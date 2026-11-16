#include "Panda/Runtime/AssetPack.hpp"

#include <Fern/Fern.hpp>
#include <Foundation/IO/StreamReaderI.hpp>

#include <cstring>
#include <fstream>

namespace Panda {

namespace {

    constexpr char MAGIC[8] = {'P', 'A', 'N', 'D', 'A', 'P', 'A', 'K'};
    constexpr uint32_t VERSION = 1;
    constexpr uint64_t ALIGNMENT = 16;
    constexpr uint64_t MAX_TABLE_SIZE = 64ull * 1024 * 1024; // a table larger than this is not a table

    // The file starts with this; the table follows it at once. Little-endian, like every platform
    // the engine runs on.
    struct Header {
        char magic[8];
        uint32_t version;
        uint32_t entryCount;
        uint64_t tableSize; // bytes of the table of contents
    };

    // An entry of the table: offset, size, the length of the path, the path.
    constexpr size_t ENTRY_FIXED_SIZE = sizeof(uint64_t) * 2 + sizeof(uint16_t);

    uint64_t aligned(uint64_t offset) {
        return (offset + ALIGNMENT - 1) / ALIGNMENT * ALIGNMENT;
    }

    template<typename T>
    void append(std::string &out, const T &value) {
        out.append(reinterpret_cast<const char *>(&value), sizeof(T));
    }

} // namespace

/* ------------------------------ Reading ------------------------------ */

AssetPack::~AssetPack() {
    if (m_reader != nullptr) { Fern::disposeResourceReader(m_reader); }
}

bool AssetPack::open(const path_t &file) {
    // The reader of the platform: a pack in the bundle of a mobile game is not a file of the
    // file system.
    m_reader = Fern::createStaticResourceReader(file);
    if (m_reader == nullptr || !m_reader->isStreamGood() || m_reader->totalSize() < sizeof(Header)) { return false; }
    Header header{};
    if (!m_reader->readData(&header, sizeof(header))) { return false; }
    if (std::memcmp(header.magic, MAGIC, sizeof(MAGIC)) != 0 || header.version != VERSION) { return false; }
    if (header.tableSize > MAX_TABLE_SIZE || sizeof(Header) + header.tableSize > m_reader->totalSize()) { return false; }
    std::string table(static_cast<size_t>(header.tableSize), '\0');
    if (!table.empty() && !m_reader->readData(table.data(), table.size())) { return false; }

    m_entries.clear();
    m_index.clear();
    m_entries.reserve(header.entryCount);
    size_t cursor = 0;
    for (uint32_t i = 0; i < header.entryCount; i++) {
        if (cursor + ENTRY_FIXED_SIZE > table.size()) { return false; }
        Entry entry;
        uint16_t pathLength = 0;
        std::memcpy(&entry.offset, table.data() + cursor, sizeof(uint64_t));
        std::memcpy(&entry.size, table.data() + cursor + sizeof(uint64_t), sizeof(uint64_t));
        std::memcpy(&pathLength, table.data() + cursor + sizeof(uint64_t) * 2, sizeof(uint16_t));
        cursor += ENTRY_FIXED_SIZE;
        if (cursor + pathLength > table.size()) { return false; }
        entry.path.assign(table.data() + cursor, pathLength);
        cursor += pathLength;
        if (entry.offset + entry.size > m_reader->totalSize()) { return false; } // the pack is cut short
        m_index[entry.path] = m_entries.size();
        m_entries.push_back(std::move(entry));
    }
    return true;
}

const AssetPack::Entry *AssetPack::find(std::string_view path) const {
    const auto found = m_index.find(std::string(path));
    return found == m_index.end() ? nullptr : &m_entries[found->second];
}

Foundation::Memory AssetPack::read(const Entry &entry, bool nullTerminated) {
    if (m_reader == nullptr) { return Foundation::Memory(); }
    const size_t size = static_cast<size_t>(entry.size);
    Foundation::Memory block = Foundation::Memory::alloc(static_cast<uint32_t>(size + (nullTerminated ? 1 : 0)));
    bool read = true;
    if (size > 0) {
        std::lock_guard<std::mutex> lock(m_readMutex);
        m_reader->setStreamPosition(entry.offset);
        read = m_reader->readData(block.data, size);
    }
    if (!read) {
        block.release();
        return Foundation::Memory();
    }
    if (nullTerminated) { static_cast<char *>(block.data)[size] = '\0'; }
    return block;
}

/* ------------------------------ Writing ------------------------------ */

void AssetPack::Writer::add(std::string path, std::string bytes) {
    Entry entry;
    entry.path = std::move(path);
    entry.size = bytes.size();
    m_paths.insert(entry.path);
    m_entries.push_back(std::move(entry));
    m_sources.push_back({std::move(bytes), path_t()});
}

void AssetPack::Writer::addFile(std::string path, const path_t &source) {
    Entry entry;
    entry.path = std::move(path);
    std::error_code error;
    const uintmax_t size = std::filesystem::file_size(source, error);
    entry.size = error ? 0 : size;
    m_paths.insert(entry.path);
    m_entries.push_back(std::move(entry));
    m_sources.push_back({std::string(), source});
}

bool AssetPack::Writer::contains(std::string_view path) const {
    return m_paths.contains(std::string(path));
}

bool AssetPack::Writer::write(const path_t &packFile, std::string &error) {
    // The table first: its size depends on the paths alone, so the offsets are known before a
    // byte of the files is written.
    uint64_t tableSize = 0;
    for (const Entry &entry : m_entries) {
        if (entry.path.size() > UINT16_MAX) {
            error = "The path is too long for the pack: " + entry.path;
            return false;
        }
        tableSize += ENTRY_FIXED_SIZE + entry.path.size();
    }
    uint64_t offset = aligned(sizeof(Header) + tableSize);
    for (Entry &entry : m_entries) {
        entry.offset = offset;
        offset = aligned(offset + entry.size);
    }

    std::string head;
    Header header{};
    std::memcpy(header.magic, MAGIC, sizeof(MAGIC));
    header.version = VERSION;
    header.entryCount = static_cast<uint32_t>(m_entries.size());
    header.tableSize = tableSize;
    append(head, header);
    for (const Entry &entry : m_entries) {
        append(head, entry.offset);
        append(head, entry.size);
        append(head, static_cast<uint16_t>(entry.path.size()));
        head += entry.path;
    }

    std::ofstream file(packFile, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        error = "The pack could not be created: " + packFile.string();
        return false;
    }
    file.write(head.data(), static_cast<std::streamsize>(head.size()));
    uint64_t written = head.size();
    static const char PADDING[ALIGNMENT] = {};
    std::vector<char> buffer(1024 * 1024);
    for (size_t i = 0; i < m_entries.size(); i++) {
        const Entry &entry = m_entries[i];
        file.write(PADDING, static_cast<std::streamsize>(entry.offset - written));
        written = entry.offset;
        const Source &source = m_sources[i];
        if (source.file.empty()) {
            file.write(source.bytes.data(), static_cast<std::streamsize>(source.bytes.size()));
        } else {
            std::ifstream input(source.file, std::ios::binary);
            uint64_t left = entry.size;
            while (left > 0 && input) {
                input.read(buffer.data(), static_cast<std::streamsize>(std::min<uint64_t>(left, buffer.size())));
                const std::streamsize got = input.gcount();
                if (got <= 0) { break; }
                file.write(buffer.data(), got);
                left -= static_cast<uint64_t>(got);
            }
            if (left != 0) {
                error = "The file could not be read into the pack: " + source.file.string();
                return false;
            }
        }
        written += entry.size;
    }
    file.flush();
    if (!file.good()) {
        error = "The pack could not be written: " + packFile.string();
        return false;
    }
    return true;
}

} // namespace Panda
