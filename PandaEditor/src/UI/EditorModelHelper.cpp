#include <Panda/Extensions/Extensions.hpp>
#include <Panda/GameLogic/GameContext.hpp>
#include "UI/EditorModelHelper.hpp"

namespace Editor {

// Built-in material shaders in the order of template registration (the core, then contributions).
std::vector<std::pair<Panda::AssetId, std::string>> builtinShaderOptions() {
    std::vector<std::pair<Panda::AssetId, std::string>> options;
    for (const Panda::ShaderTemplate &shaderTemplate : Panda::GameContext::getExtensions().shaderTemplates.templates()) {
        options.push_back({shaderTemplate.shader, shaderTemplate.displayName});
    }
    return options;
}

const char *componentTypeName(ComponentType type) {
    switch (type) {
        case ComponentType::Camera: return "Camera";
        case ComponentType::DirectionalLight: return "Directional Light";
        case ComponentType::PointLight: return "Point Light";
        case ComponentType::SpriteRenderer: return "Sprite Renderer";
        case ComponentType::RuntimeMesh: return "Runtime Mesh";
        case ComponentType::PrimitiveMesh: return "Primitive Mesh";
        case ComponentType::ImportedSubmesh: return "Imported Submesh";
        case ComponentType::Sky: return "Cube Map (Sky)";
        case ComponentType::Script: return "Script";
    }
    return "Component";
}

const char *componentTypeTitle(ComponentType type) {
    switch (type) {
        case ComponentType::Camera: return "CAMERA";
        case ComponentType::DirectionalLight: return "DIRECTIONAL LIGHT";
        case ComponentType::PointLight: return "POINT LIGHT";
        case ComponentType::SpriteRenderer: return "SPRITE RENDERER";
        case ComponentType::RuntimeMesh: return "RUNTIME MESH";
        case ComponentType::PrimitiveMesh: return "PRIMITIVE MESH";
        case ComponentType::ImportedSubmesh: return "IMPORTED SUBMESH";
        case ComponentType::Sky: return "CUBE MAP (SKY)";
        case ComponentType::Script: return "SCRIPT";
    }
    return "COMPONENT";
}

std::vector<ComponentType> addableComponentTypes() {
    return {
        ComponentType::Camera,         ComponentType::DirectionalLight, ComponentType::PointLight,
        ComponentType::SpriteRenderer, ComponentType::RuntimeMesh,      ComponentType::PrimitiveMesh,
        ComponentType::ImportedSubmesh, ComponentType::Sky,             ComponentType::Script,
    };
}

// The kind string for the editor UI: built-in kinds have historical tags (world — "scene", cubemap and
// runtime mesh share the tag with texture and mesh), kinds from the registry — their name.
const char *assetTypeString(Panda::AssetType type) {
    if (type == Panda::AssetType::TEXTURE || type == Panda::AssetType::CUBE_MAP) { return "texture"; }
    if (type == Panda::AssetType::IMPORTED_MESH || type == Panda::AssetType::RUNTIME_MESH) { return "mesh"; }
    if (type == Panda::AssetType::WORLD) { return "scene"; }
    if (const Panda::AssetKind *kind = Panda::GameContext::getExtensions().assetKinds.find(type)) {
        return kind->name.c_str();
    }
    return "text"; // NONE — a generic file
}

const char *assetTypeFolder(Panda::AssetType type) {
    if (const Panda::AssetKind *kind = Panda::GameContext::getExtensions().assetKinds.find(type)) {
        return kind->folder.c_str();
    }
    return "Other";
}

} // namespace Editor
