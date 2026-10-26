#include "Panda/Assets/Base/CoreAssetKinds.hpp"

#include "Panda/Assets/FontAsset.hpp"

namespace Panda {

namespace {

    AssetKind builtin(
        const char *name, AssetType type, const char *displayName, const char *folder,
        std::vector<std::string> extensions
    ) {
        AssetKind kind;
        kind.name = name;
        kind.type = type;
        kind.displayName = displayName;
        kind.folder = folder;
        kind.extensions = std::move(extensions);
        return kind;
    }

} // namespace

void registerCoreAssetKinds(AssetKindRegistry &registry) {
    registry.add(builtin("texture", AssetType::TEXTURE, "Texture", "Textures", {".png", ".jpeg", ".jpg", ".hdr"}));
    registry.add(builtin("cubemap", AssetType::CUBE_MAP, "Cube Map", "Textures", {}));
    registry.add(builtin("shader", AssetType::SHADER, "Shader", "Shaders", {".hlsl"}));
    registry.add(builtin("material", AssetType::MATERIAL, "Material", "Materials", {".mat"}));
    registry.add(builtin("runtimeMesh", AssetType::RUNTIME_MESH, "Mesh", "Meshes", {}));
    registry.add(builtin("mesh", AssetType::IMPORTED_MESH, "Mesh", "Meshes", {".gltf"}));
    registry.add(builtin("world", AssetType::WORLD, "World", "Worlds", {".world"}));
    registry.add(builtin("postfx", AssetType::POSTPROCESSING, "Postprocessing", "Postprocessing", {".postfx"}));
    // Fonts are a file-based kind: the editor and the runtime read the .ttf as it is and the export
    // copies it (FontAsset); the hosts rasterize what they show.
    AssetKind font = builtin("font", AssetType::FONT, "Font", "Fonts", {".ttf", ".otf", ".ttc"});
    font.load = FontAssetKind::load;
    font.placeholder = FontAssetKind::placeholder;
    font.save = FontAssetKind::save;
    registry.add(std::move(font));
}

} // namespace Panda
