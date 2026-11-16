#pragma once

#include "Panda/Assets/Base/Path.hpp"

#include <Foundation/Foundation.hpp>

#include <cstddef>
#include <istream>
#include <memory>

namespace Panda {

class AssetPack;

// The files an engine loader reads at run time — a cooked texture, a world, a shader, a file of a
// module kind. In the editor they are files of the project. In a game they are entries of its
// pack (AssetPack): the runtime mounts the pack at the folder of the cooked data, and a path
// under that folder is answered from the pack from then on. A loader keeps taking a path and
// knows nothing about packs: it opens the file here instead of the file system.
namespace GameFiles {

    // The files of `root` are the entries of the pack from now on (one pack at a time).
    void mount(const path_t &root, std::unique_ptr<AssetPack> pack);
    void unmount();
    // The pack that is mounted; nullptr — none (the editor).
    AssetPack *mounted();

    // The path names an entry of the mounted pack.
    bool isPacked(const path_t &path);
    bool exists(const path_t &path);
    // The whole file in a block the caller releases; outSize — its length. An empty block
    // (data == nullptr) — no such file. nullTerminated adds a zero byte after the data.
    Foundation::Memory read(const path_t &path, size_t &outSize, bool nullTerminated = false);
    // The file as a binary stream; nullptr — no such file.
    std::unique_ptr<std::istream> open(const path_t &path);

} // namespace GameFiles

} // namespace Panda
