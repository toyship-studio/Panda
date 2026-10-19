#pragma once

#include <Panda/Assets/Base/Asset.hpp> // Panda::AssetType

#include <string>
#include <utility>
#include <vector>

namespace Editor {

// The editor enumeration of CORE component types — for the Add Component menu, inspector section
// titles and the add/remove dispatch. The component data itself lives in the engine (Panda::*Component);
// there is no intermediate model. Module components go by name through the
// EditorExtensions::componentInspectors registry.
enum class ComponentType {
    Camera,
    DirectionalLight,
    PointLight,
    SpriteRenderer,
    RuntimeMesh,
    PrimitiveMesh,
    ImportedSubmesh,
    Sky,
    Script,
};

const char *componentTypeName(ComponentType type);  // "Camera", "Point Light", ... (menu items)
const char *componentTypeTitle(ComponentType type); // UPPERCASE — inspector section titles
std::vector<ComponentType> addableComponentTypes(); // types for the "Add Component" menu

// Built-in engine shaders for materials (StaticResources), from simple to complex.
// The id is persisted in the .mat → the material survives a restart (static IDs are stable).
std::vector<std::pair<Panda::AssetId, std::string>> builtinShaderOptions();

// A built-in postprocessing PASS shader: no thumbnails/preview sphere are drawn for materials that use it

// Engine asset type → kind string (icon/color of panel cells, kind in the D&D payload) and the name
// of the category folder (AssetManager).
const char *assetTypeString(Panda::AssetType type); // "texture"/"material"/"mesh"/"scene"/"shader"/"text"
const char *assetTypeFolder(Panda::AssetType type); // "Textures"/"Materials"/"Meshes"/"Worlds"/"Shaders"/"Other"

} // namespace Editor
