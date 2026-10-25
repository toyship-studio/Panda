#include "Project/System/ZipArchive.hpp"

#include <miniz.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace Panda::ZipArchive {

namespace {

    std::string utf8(const path_t &path) {
        const std::u8string text = path.generic_u8string();
        return std::string(text.begin(), text.end());
    }

    path_t fromUtf8(const std::string &text) {
        return path_t(std::u8string(reinterpret_cast<const char8_t *>(text.data()), text.size()));
    }

    FILE *openFile(const path_t &path, const char *mode) {
#if defined(_WIN32)
        const std::wstring wideMode(mode, mode + std::char_traits<char>::length(mode));
        return _wfopen(path.c_str(), wideMode.c_str());
#else
        return std::fopen(path.c_str(), mode);
#endif
    }

    std::string zipError(mz_zip_archive &zip, const char *what) {
        return std::string(what) + ": " + mz_zip_get_error_string(mz_zip_get_last_error(&zip));
    }

    // The entry name as a relative path inside the destination, or nothing for a name that would
    // leave it (absolute, a drive, ".."): such an archive is refused.
    std::optional<path_t> safeRelativePath(std::string name) {
        for (char &c : name) {
            if (c == '\\') { c = '/'; }
        }
        if (name.empty() || name.front() == '/' || name.find(':') != std::string::npos) { return std::nullopt; }
        path_t relative;
        size_t start = 0;
        while (start <= name.size()) {
            const size_t end = name.find('/', start);
            const std::string component = name.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (component == "..") { return std::nullopt; }
            if (!component.empty() && component != ".") { relative /= fromUtf8(component); }
            if (end == std::string::npos) { break; }
            start = end + 1;
        }
        if (relative.empty()) { return std::nullopt; }
        return relative;
    }

    struct Reader {
        mz_zip_archive zip = {};
        FILE *file = nullptr;
        bool open = false;

        bool init(const path_t &archive, std::string &error) {
            std::error_code code;
            const uint64_t size = std::filesystem::file_size(archive, code);
            if (code) {
                error = "The archive cannot be read: " + code.message();
                return false;
            }
            file = openFile(archive, "rb");
            if (!file) {
                error = "The archive cannot be opened";
                return false;
            }
            if (!mz_zip_reader_init_cfile(&zip, file, size, 0)) {
                error = zipError(zip, "Not a zip archive");
                return false;
            }
            open = true;
            return true;
        }

        ~Reader() {
            if (open) { mz_zip_reader_end(&zip); }
            if (file) { std::fclose(file); }
        }
    };

    size_t writeToStream(void *opaque, mz_uint64, const void *buffer, size_t size) {
        auto *stream = static_cast<std::ofstream *>(opaque);
        stream->write(static_cast<const char *>(buffer), static_cast<std::streamsize>(size));
        return stream->good() ? size : 0;
    }

} // namespace

bool extract(const path_t &archive, const path_t &destination, std::string &error) {
    Reader reader;
    if (!reader.init(archive, error)) { return false; }
    std::error_code code;
    std::filesystem::create_directories(destination, code);
    if (code) {
        error = "The destination cannot be created: " + code.message();
        return false;
    }
    const mz_uint count = mz_zip_reader_get_num_files(&reader.zip);
    for (mz_uint index = 0; index < count; ++index) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&reader.zip, index, &stat)) {
            error = zipError(reader.zip, "A damaged entry");
            return false;
        }
        const std::optional<path_t> relative = safeRelativePath(stat.m_filename);
        if (!relative) {
            error = std::string("The archive refers outside its folder: ") + stat.m_filename;
            return false;
        }
        const path_t target = destination / *relative;
        if (stat.m_is_directory) {
            std::filesystem::create_directories(target, code);
            continue;
        }
        std::filesystem::create_directories(target.parent_path(), code);
        std::ofstream stream(target, std::ios::binary | std::ios::trunc);
        if (!stream.is_open()) {
            error = "Cannot write " + utf8(target);
            return false;
        }
        if (!mz_zip_reader_extract_to_callback(&reader.zip, index, &writeToStream, &stream, 0) || !stream.good()) {
            error = "Cannot unpack " + std::string(stat.m_filename) + ": " +
                    mz_zip_get_error_string(mz_zip_get_last_error(&reader.zip));
            return false;
        }
    }
    return true;
}

std::optional<std::vector<uint8_t>> read(const path_t &archive, const std::string &entryName, std::string &error) {
    Reader reader;
    if (!reader.init(archive, error)) { return std::nullopt; }
    const int index = mz_zip_reader_locate_file(&reader.zip, entryName.c_str(), nullptr, 0);
    if (index < 0) {
        error = "The archive has no " + entryName;
        return std::nullopt;
    }
    size_t size = 0;
    void *bytes = mz_zip_reader_extract_to_heap(&reader.zip, static_cast<mz_uint>(index), &size, 0);
    if (!bytes) {
        error = "Cannot unpack " + entryName + ": " + mz_zip_get_error_string(mz_zip_get_last_error(&reader.zip));
        return std::nullopt;
    }
    std::vector<uint8_t> data(static_cast<const uint8_t *>(bytes), static_cast<const uint8_t *>(bytes) + size);
    mz_free(bytes);
    return data;
}

std::optional<std::vector<std::string>> list(const path_t &archive, std::string &error) {
    Reader reader;
    if (!reader.init(archive, error)) { return std::nullopt; }
    std::vector<std::string> names;
    const mz_uint count = mz_zip_reader_get_num_files(&reader.zip);
    for (mz_uint index = 0; index < count; ++index) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&reader.zip, index, &stat)) {
            error = zipError(reader.zip, "A damaged entry");
            return std::nullopt;
        }
        names.emplace_back(stat.m_filename);
    }
    return names;
}

bool create(const path_t &folder, const path_t &archive, std::string &error) {
    std::error_code code;
    if (!std::filesystem::is_directory(folder, code)) {
        error = "Not a folder: " + utf8(folder);
        return false;
    }
    std::filesystem::remove(archive, code);
    FILE *file = openFile(archive, "wb");
    if (!file) {
        error = "Cannot write " + utf8(archive);
        return false;
    }
    mz_zip_archive zip = {};
    if (!mz_zip_writer_init_cfile(&zip, file, 0)) {
        error = zipError(zip, "Cannot start the archive");
        std::fclose(file);
        return false;
    }
    bool ok = true;
    const std::string root = utf8(folder.filename());
    ok = mz_zip_writer_add_mem(&zip, (root + "/").c_str(), nullptr, 0, 0);
    for (auto it = std::filesystem::recursive_directory_iterator(folder, code); ok && !code && it != std::filesystem::recursive_directory_iterator(); it.increment(code)) {
        const path_t path = it->path();
        if (path.filename() == ".DS_Store") { continue; }
        const std::string name = root + "/" + utf8(std::filesystem::relative(path, folder, code));
        if (it->is_directory(code)) {
            ok = mz_zip_writer_add_mem(&zip, (name + "/").c_str(), nullptr, 0, 0);
            continue;
        }
        if (!it->is_regular_file(code)) { continue; }
        std::ifstream stream(path, std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        if (!stream.good() && !stream.eof()) {
            error = "Cannot read " + utf8(path);
            ok = false;
            break;
        }
        ok = mz_zip_writer_add_mem(&zip, name.c_str(), bytes.data(), bytes.size(), MZ_DEFAULT_LEVEL);
    }
    if (ok && error.empty()) { ok = mz_zip_writer_finalize_archive(&zip); }
    if (!ok && error.empty()) { error = zipError(zip, "Cannot write the archive"); }
    mz_zip_writer_end(&zip);
    std::fclose(file);
    if (!ok) { std::filesystem::remove(archive, code); }
    return ok;
}

} // namespace Panda::ZipArchive
