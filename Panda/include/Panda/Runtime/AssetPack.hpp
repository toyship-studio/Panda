#pragma once

#include "Panda/Assets/Base/Path.hpp"

#include <Foundation/Foundation.hpp>

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Foundation {
class StreamReaderI;
}

namespace Panda {

// The pack of a game: every file its runtime reads, in one file. A header, a table of contents
// (the path of a file → where it starts and how long it is) and the files as the cook made them,
// each starting at a multiple of 16 bytes. Nothing is compressed and nothing is decoded: a file
// of the pack is one seek and one read, and starting a game costs the table — a few kilobytes.
// Paths are relative, with forward slashes, the way the cooked data is laid out
// ("Worlds/main.pworld", "Textures/Assets/crate.ptex").
//
// The editor writes it (Writer), a game reads it (GameFiles mounts one).
class AssetPack final {
public:
    static constexpr const char *FILE_NAME = "Assets.pak";

    struct Entry {
        std::string path;
        uint64_t offset = 0;
        uint64_t size = 0;
    };

    AssetPack() = default;
    ~AssetPack();
    AssetPack(const AssetPack &) = delete;
    AssetPack &operator=(const AssetPack &) = delete;

    // false — the file is not there or is not a pack of this version.
    bool open(const path_t &file);
    const std::vector<Entry> &entries() const {
        return m_entries;
    }
    const Entry *find(std::string_view path) const;
    // The whole file in a block the caller releases (empty — the read failed). nullTerminated
    // adds a zero byte after the data: a text the loader hands to a parser as a C string.
    Foundation::Memory read(const Entry &entry, bool nullTerminated = false);

    // The files of a pack in the order they are added; write lays them out and writes the pack.
    class Writer final {
    public:
        // A file made in memory (a world, a material, the registry in their binary form).
        void add(std::string path, std::string bytes);
        // A file on disk, copied in as it is (a cooked texture, a shader, a file of a module).
        void addFile(std::string path, const path_t &source);
        bool contains(std::string_view path) const;
        // What was added so far: the sizes are known, the offsets — after write.
        const std::vector<Entry> &entries() const {
            return m_entries;
        }
        // false — a source file could not be read or the pack could not be written (error says which).
        bool write(const path_t &packFile, std::string &error);

    private:
        struct Source {
            std::string bytes; // of a file made in memory
            path_t file;       // or the file on disk
        };
        std::vector<Entry> m_entries;
        std::vector<Source> m_sources;
        std::unordered_set<std::string> m_paths; // of the entries: contains is asked for every file
    };

private:
    Foundation::StreamReaderI *m_reader = nullptr;
    std::vector<Entry> m_entries;
    std::unordered_map<std::string, size_t> m_index; // path → entry
    std::mutex m_readMutex;                           // a read is a seek and a read of one stream
};

} // namespace Panda
