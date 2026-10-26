#include "Fern/Fonts/FontCatalog.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <system_error>
#include <unordered_set>

namespace Fern::Fonts {

namespace {

    // The sfnt tables we read. Big-endian throughout.
    uint16_t readU16(const unsigned char *p) {
        return static_cast<uint16_t>((p[0] << 8) | p[1]);
    }
    uint32_t readU32(const unsigned char *p) {
        return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 8) | p[3];
    }

    struct TableRecord {
        uint32_t tag = 0;
        uint32_t offset = 0;
        uint32_t length = 0;
    };

    constexpr uint32_t tag(const char (&name)[5]) {
        return (static_cast<uint32_t>(name[0]) << 24) | (static_cast<uint32_t>(name[1]) << 16) | (static_cast<uint32_t>(name[2]) << 8) | static_cast<uint32_t>(name[3]);
    }

    bool readAt(std::ifstream &file, uint64_t offset, std::vector<unsigned char> &out, size_t length) {
        if (length == 0 || length > (64u << 20)) { return false; }
        out.resize(length);
        file.clear();
        file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        file.read(reinterpret_cast<char *>(out.data()), static_cast<std::streamsize>(length));
        return static_cast<size_t>(file.gcount()) == length;
    }

    // UTF-16BE (the Windows platform of the name table) → UTF-8.
    std::string utf16beToUtf8(const unsigned char *data, size_t length) {
        std::string out;
        for (size_t i = 0; i + 1 < length; i += 2) {
            uint32_t codepoint = readU16(data + i);
            if (codepoint >= 0xD800 && codepoint <= 0xDBFF && i + 3 < length) {
                const uint32_t low = readU16(data + i + 2);
                if (low >= 0xDC00 && low <= 0xDFFF) {
                    codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
                    i += 2;
                }
            }
            if (codepoint < 0x80) {
                out += static_cast<char>(codepoint);
            } else if (codepoint < 0x800) {
                out += static_cast<char>(0xC0 | (codepoint >> 6));
                out += static_cast<char>(0x80 | (codepoint & 0x3F));
            } else if (codepoint < 0x10000) {
                out += static_cast<char>(0xE0 | (codepoint >> 12));
                out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (codepoint & 0x3F));
            } else {
                out += static_cast<char>(0xF0 | (codepoint >> 18));
                out += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
                out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (codepoint & 0x3F));
            }
        }
        return out;
    }

    // The best string of a name id: Windows platform, English first; else the Mac Roman one (ASCII).
    std::string nameString(const std::vector<unsigned char> &table, uint16_t nameId) {
        if (table.size() < 6) { return {}; }
        const uint16_t count = readU16(table.data() + 2);
        const uint16_t stringOffset = readU16(table.data() + 4);
        std::string best;
        int bestScore = -1;
        for (uint16_t i = 0; i < count; ++i) {
            const size_t record = 6 + static_cast<size_t>(i) * 12;
            if (record + 12 > table.size()) { break; }
            const unsigned char *r = table.data() + record;
            const uint16_t platform = readU16(r), encoding = readU16(r + 2), language = readU16(r + 4), id = readU16(r + 6);
            const uint16_t length = readU16(r + 8), offset = readU16(r + 10);
            if (id != nameId) { continue; }
            const size_t begin = static_cast<size_t>(stringOffset) + offset;
            if (begin + length > table.size()) { continue; }
            int score = -1;
            std::string value;
            if (platform == 3 && (encoding == 1 || encoding == 10)) {
                score = language == 0x0409 ? 3 : 2;
                value = utf16beToUtf8(table.data() + begin, length);
            } else if (platform == 0) {
                score = 1;
                value = utf16beToUtf8(table.data() + begin, length);
            } else if (platform == 1 && encoding == 0) {
                score = 0;
                for (uint16_t k = 0; k < length; ++k) {
                    const unsigned char c = table[begin + k];
                    if (c < 0x80) { value += static_cast<char>(c); }
                }
            }
            if (score > bestScore && !value.empty()) {
                bestScore = score;
                best = std::move(value);
            }
        }
        return best;
    }

    // One face of a file (at the offset of its table directory) → a Face, or nullopt.
    std::optional<Face> readFace(std::ifstream &file, uint32_t directoryOffset, const std::filesystem::path &path, int index) {
        std::vector<unsigned char> header;
        if (!readAt(file, directoryOffset, header, 12)) { return std::nullopt; }
        const uint32_t version = readU32(header.data());
        if (version != 0x00010000 && version != tag("OTTO") && version != tag("true")) { return std::nullopt; }
        const uint16_t numTables = readU16(header.data() + 4);
        if (numTables == 0 || numTables > 64) { return std::nullopt; }
        std::vector<unsigned char> records;
        if (!readAt(file, directoryOffset + 12, records, static_cast<size_t>(numTables) * 16)) { return std::nullopt; }
        TableRecord name, os2, head;
        for (uint16_t i = 0; i < numTables; ++i) {
            const unsigned char *r = records.data() + static_cast<size_t>(i) * 16;
            TableRecord record{readU32(r), readU32(r + 8), readU32(r + 12)};
            if (record.tag == tag("name")) { name = record; }
            if (record.tag == tag("OS/2")) { os2 = record; }
            if (record.tag == tag("head")) { head = record; }
        }
        if (name.length == 0) { return std::nullopt; }
        std::vector<unsigned char> nameTable;
        if (!readAt(file, name.offset, nameTable, std::min<uint32_t>(name.length, 1u << 20))) { return std::nullopt; }

        Face face;
        face.family = nameString(nameTable, 16); // the typographic family first: "Inter", not "Inter Display Bold"
        if (face.family.empty()) { face.family = nameString(nameTable, 1); }
        face.style = nameString(nameTable, 17);
        if (face.style.empty()) { face.style = nameString(nameTable, 2); }
        if (face.family.empty() || face.family[0] == '.') { return std::nullopt; } // hidden system faces
        face.file = path;
        face.index = index;

        std::vector<unsigned char> os2Table;
        if (os2.length >= 64 && readAt(file, os2.offset, os2Table, 64)) {
            face.weight = readU16(os2Table.data() + 4);
            face.italic = (readU16(os2Table.data() + 62) & 0x1) != 0;
        } else {
            std::vector<unsigned char> headTable;
            if (head.length >= 46 && readAt(file, head.offset, headTable, 46)) {
                const uint16_t macStyle = readU16(headTable.data() + 44);
                face.weight = (macStyle & 0x1) ? 700 : 400;
                face.italic = (macStyle & 0x2) != 0;
            }
        }
        if (face.weight < 100 || face.weight > 900) { face.weight = 400; }
        return face;
    }

    void readFile(const std::filesystem::path &path, std::vector<Face> &out) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) { return; }
        std::vector<unsigned char> header;
        if (!readAt(file, 0, header, 12)) { return; }
        if (readU32(header.data()) == tag("ttcf")) {
            const uint32_t count = std::min<uint32_t>(readU32(header.data() + 8), 64);
            std::vector<unsigned char> offsets;
            if (!readAt(file, 12, offsets, static_cast<size_t>(count) * 4)) { return; }
            for (uint32_t i = 0; i < count; ++i) {
                if (std::optional<Face> face = readFace(file, readU32(offsets.data() + i * 4), path, static_cast<int>(i))) {
                    out.push_back(std::move(*face));
                }
            }
            return;
        }
        if (std::optional<Face> face = readFace(file, 0, path, 0)) { out.push_back(std::move(*face)); }
    }

    bool isFontFile(const std::filesystem::path &path) {
        std::string extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return extension == ".ttf" || extension == ".otf" || extension == ".ttc";
    }

    std::filesystem::path home() {
#if defined(_WIN32)
        const char *profile = std::getenv("USERPROFILE");
        return profile ? std::filesystem::path(profile) : std::filesystem::path();
#else
        const char *home = std::getenv("HOME");
        return home ? std::filesystem::path(home) : std::filesystem::path();
#endif
    }

    struct Catalog {
        std::vector<Face> faces;
        std::vector<std::string> families;
    };

    Catalog scan() {
        Catalog catalog;
        std::unordered_set<std::string> seen; // family + weight + italic: the first file wins
        for (const std::filesystem::path &directory : directories()) {
            std::error_code error;
            if (!std::filesystem::is_directory(directory, error)) { continue; }
            for (std::filesystem::recursive_directory_iterator it(directory, std::filesystem::directory_options::skip_permission_denied, error), end;
                 it != end; it.increment(error)) {
                if (error) { break; }
                if (!it->is_regular_file(error) || !isFontFile(it->path())) { continue; }
                std::vector<Face> found;
                readFile(it->path(), found);
                for (Face &face : found) {
                    const std::string key = face.family + "\n" + std::to_string(face.weight) + (face.italic ? "i" : "");
                    if (!seen.insert(key).second) { continue; }
                    catalog.faces.push_back(std::move(face));
                }
            }
        }
        std::sort(catalog.faces.begin(), catalog.faces.end(), [](const Face &a, const Face &b) {
            if (a.family != b.family) { return a.family < b.family; }
            if (a.italic != b.italic) { return !a.italic; }
            return a.weight < b.weight;
        });
        for (const Face &face : catalog.faces) {
            if (catalog.families.empty() || catalog.families.back() != face.family) { catalog.families.push_back(face.family); }
        }
        return catalog;
    }

    std::mutex s_mutex;
    bool s_scanned = false;
    Catalog s_catalog;

    const Catalog &catalog() {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (!s_scanned) {
            s_catalog = scan();
            s_scanned = true;
        }
        return s_catalog;
    }

} // namespace

std::vector<std::filesystem::path> directories() {
    std::vector<std::filesystem::path> result;
    const std::filesystem::path user = home();
#if defined(__APPLE__)
    result.push_back("/System/Library/Fonts");
    result.push_back("/System/Library/Fonts/Supplemental");
    result.push_back("/Library/Fonts");
    if (!user.empty()) { result.push_back(user / "Library" / "Fonts"); }
#elif defined(_WIN32)
    if (const char *windir = std::getenv("WINDIR")) { result.push_back(std::filesystem::path(windir) / "Fonts"); }
    if (const char *local = std::getenv("LOCALAPPDATA")) { result.push_back(std::filesystem::path(local) / "Microsoft" / "Windows" / "Fonts"); }
#else
    result.push_back("/usr/share/fonts");
    result.push_back("/usr/local/share/fonts");
    if (!user.empty()) {
        result.push_back(user / ".fonts");
        result.push_back(user / ".local" / "share" / "fonts");
    }
#endif
    return result;
}

const std::vector<std::string> &families() {
    return catalog().families;
}

const std::vector<Face> &faces() {
    return catalog().faces;
}

std::optional<Face> find(std::string_view family, int weight, bool italic) {
    const Catalog &all = catalog();
    const Face *best = nullptr;
    int bestDistance = 0;
    for (const Face &face : all.faces) {
        if (face.family != family) { continue; }
        // The closest weight wins; the wrong slant costs as much as three weight steps.
        const int distance = std::abs(face.weight - weight) + (face.italic != italic ? 300 : 0);
        if (!best || distance < bestDistance) {
            best = &face;
            bestDistance = distance;
        }
    }
    if (!best) { return std::nullopt; }
    return *best;
}

std::vector<Face> facesOf(const std::filesystem::path &file) {
    std::vector<Face> result;
    readFile(file, result);
    return result;
}

void rescan() {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_catalog = scan();
    s_scanned = true;
}

} // namespace Fern::Fonts
