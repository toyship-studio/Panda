#include "Panda/Runtime/GameFiles.hpp"

#include "Panda/Runtime/AssetPack.hpp"

#include <fstream>
#include <streambuf>

namespace Panda::GameFiles {

namespace {

    path_t s_root;
    std::unique_ptr<AssetPack> s_pack;

    // The entry a path names: the path is under the mounted folder and the pack has it.
    const AssetPack::Entry *entryOf(const path_t &path) {
        if (!s_pack) { return nullptr; }
        const path_t relative = path.lexically_normal().lexically_relative(s_root);
        if (relative.empty() || *relative.begin() == "..") { return nullptr; }
        return s_pack->find(relative.generic_string());
    }

    // A stream over a block of memory it owns.
    class MemoryStream final : private std::streambuf, public std::istream {
    public:
        MemoryStream(Foundation::Memory block, size_t size)
            : std::istream(static_cast<std::streambuf *>(this))
            , m_block(block) {
            char *begin = static_cast<char *>(m_block.data);
            setg(begin, begin, begin + size);
        }
        ~MemoryStream() override {
            m_block.release();
        }

    protected:
        // tellg and seekg: a loader measures a file by seeking to its end. The types are named
        // in full: both bases have a pos_type and an off_type, and to GCC and MSVC a bare one
        // is ambiguous.
        std::streampos seekoff(std::streamoff offset, std::ios_base::seekdir direction, std::ios_base::openmode) override {
            char *target = direction == std::ios_base::beg   ? eback() + offset
                           : direction == std::ios_base::cur ? gptr() + offset
                                                             : egptr() + offset;
            if (target < eback() || target > egptr()) { return std::streampos(std::streamoff(-1)); }
            setg(eback(), target, egptr());
            return std::streampos(target - eback());
        }
        std::streampos seekpos(std::streampos position, std::ios_base::openmode mode) override {
            return seekoff(std::streamoff(position), std::ios_base::beg, mode);
        }

    private:
        Foundation::Memory m_block;
    };

} // namespace

void mount(const path_t &root, std::unique_ptr<AssetPack> pack) {
    s_root = root.lexically_normal();
    s_pack = std::move(pack);
}

void unmount() {
    s_pack.reset();
    s_root.clear();
}

AssetPack *mounted() {
    return s_pack.get();
}

bool isPacked(const path_t &path) {
    return entryOf(path) != nullptr;
}

bool exists(const path_t &path) {
    if (entryOf(path) != nullptr) { return true; }
    std::error_code error;
    return std::filesystem::is_regular_file(path, error);
}

Foundation::Memory read(const path_t &path, size_t &outSize, bool nullTerminated) {
    outSize = 0;
    if (const AssetPack::Entry *entry = entryOf(path)) {
        Foundation::Memory block = s_pack->read(*entry, nullTerminated);
        if (block.data != nullptr) { outSize = static_cast<size_t>(entry->size); }
        return block;
    }
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) { return Foundation::Memory(); }
    const std::streamsize size = file.tellg();
    if (size < 0) { return Foundation::Memory(); }
    Foundation::Memory block = Foundation::Memory::alloc(static_cast<uint32_t>(size) + (nullTerminated ? 1 : 0));
    file.seekg(0, std::ios::beg);
    if (size > 0) { file.read(static_cast<char *>(block.data), size); }
    if (!file) {
        block.release();
        return Foundation::Memory();
    }
    if (nullTerminated) { static_cast<char *>(block.data)[size] = '\0'; }
    outSize = static_cast<size_t>(size);
    return block;
}

std::unique_ptr<std::istream> open(const path_t &path) {
    if (const AssetPack::Entry *entry = entryOf(path)) {
        Foundation::Memory block = s_pack->read(*entry);
        if (block.data == nullptr) { return nullptr; }
        return std::make_unique<MemoryStream>(block, static_cast<size_t>(entry->size));
    }
    auto file = std::make_unique<std::ifstream>(path, std::ios::binary);
    if (!file->is_open()) { return nullptr; }
    return file;
}

} // namespace Panda::GameFiles
