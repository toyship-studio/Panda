#pragma once

#include <Panda/Assets/Base/Asset.hpp> // Panda::AssetType

#include <string>
#include <utility>
#include <vector>

namespace Editor {

// Built-in engine shaders for materials (StaticResources), from simple to complex.
// The id is persisted in the .mat → the material survives a restart (static IDs are stable).
std::vector<std::pair<Panda::AssetId, std::string>> builtinShaderOptions();

// Engine asset type → kind string (icon/color of panel cells, kind in the D&D payload).
const char *assetTypeString(Panda::AssetType type); // "texture"/"material"/"mesh"/"world"/"shader"/"text"

} // namespace Editor
