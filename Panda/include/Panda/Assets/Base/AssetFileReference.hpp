#pragma once

#include "Panda/Assets/Base/Path.hpp"

#include <Rain/Rain.hpp>

#include <string>

namespace Panda {

// The source of an asset in the project: a file + a sub-path inside it. A regular asset is a whole
// file (subPath is empty); container formats (glb: one file carries a mesh, textures, materials)
// address a sub-asset by subPath ("images/3"). The registries store id → AssetFileReference:
// "where it lives" is a property of the registration, the asset does not know about its source.
struct AssetFileReference : public Rain::Codable {
    path_t path;
    std::string subPath;

    AssetFileReference() = default;
    AssetFileReference(path_t filePath)
        : path(std::move(filePath)) {}
    AssetFileReference(path_t filePath, std::string sub)
        : path(std::move(filePath))
        , subPath(std::move(sub)) {}

    bool empty() const {
        return path.empty();
    }

    RAIN_FIELDS_BEGIN(AssetFileReference)
    RAIN_FIELD(path)
    RAIN_FIELD(subPath)
    RAIN_FIELDS_END
};

} // namespace Panda
