#include "Panda/Runtime/CheckRun.hpp"

#include <cstring>
#include <fstream>

namespace Panda {

namespace {

    constexpr char MAGIC[4] = {'P', 'S', 'H', 'T'};
    constexpr uint32_t MAX_SIDE = 16384;

    struct Header {
        char magic[4];
        uint32_t width;
        uint32_t height;
        uint32_t bottomUp;
    };

} // namespace

bool CheckShot::write(const path_t &file) const {
    if (pixels.size() != static_cast<size_t>(width) * height * 4) { return false; }
    std::ofstream stream(file, std::ios::binary | std::ios::trunc);
    if (!stream.is_open()) { return false; }
    Header header{};
    std::memcpy(header.magic, MAGIC, sizeof(MAGIC));
    header.width = width;
    header.height = height;
    header.bottomUp = bottomUp ? 1u : 0u;
    stream.write(reinterpret_cast<const char *>(&header), sizeof(header));
    stream.write(reinterpret_cast<const char *>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
    return stream.good();
}

bool CheckShot::read(const path_t &file) {
    std::ifstream stream(file, std::ios::binary);
    Header header{};
    stream.read(reinterpret_cast<char *>(&header), sizeof(header));
    if (!stream || std::memcmp(header.magic, MAGIC, sizeof(MAGIC)) != 0 || header.width == 0 || header.height == 0 ||
        header.width > MAX_SIDE || header.height > MAX_SIDE) {
        return false;
    }
    width = header.width;
    height = header.height;
    bottomUp = header.bottomUp != 0;
    pixels.resize(static_cast<size_t>(width) * height * 4);
    stream.read(reinterpret_cast<char *>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
    return static_cast<bool>(stream);
}

} // namespace Panda
