#pragma once

#include <Panda/Assets/Base/AssetInfo.hpp>
#include <Panda/Assets/Base/Path.hpp>

namespace Panda {

// The format of the X.<ext>.pmeta sidecar (context/asset-files.md): a serialized AssetInfo —
// id + type + settings. There are no paths inside at all (their source = the location of the pair
// on disk), so moving the pair does not change the file content, and sidecars of old formats with
// paths inside are read as is (unknown keys in Rain-JSON are ignored).
namespace AssetPmeta {

    constexpr const char *EXTENSION = ".pmeta";

    // X.<ext> -> X.<ext>.pmeta
    path_t sidecarPathFor(const path_t &assetFilePath);

    // false — the file could not be opened or the type is unknown (a broken sidecar).
    bool read(const path_t &sidecarAbsPath, AssetInfo &out);
    bool write(const path_t &sidecarAbsPath, const AssetInfo &info);

} // namespace AssetPmeta

} // namespace Panda
