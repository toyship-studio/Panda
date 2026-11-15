#include <Panda/Extensions/Extensions.hpp>
#include <Panda/GameLogic/GameContext.hpp>
#include "UI/EditorModelHelper.hpp"

#include <PandaEditorKit/Widgets/Icons.hpp>

namespace Editor {

// Built-in material shaders in the order of template registration (the core, then contributions).
std::vector<std::pair<Panda::AssetId, std::string>> builtinShaderOptions() {
    std::vector<std::pair<Panda::AssetId, std::string>> options;
    for (const Panda::ShaderTemplate &shaderTemplate : Panda::GameContext::getExtensions().shaderTemplates.templates()) {
        options.push_back({shaderTemplate.shader, shaderTemplate.displayName});
    }
    return options;
}

// The kind string for the editor UI: the name of the kind in the registry (Extensions::assetKinds).
// A cube map and a runtime mesh answer as a texture and a mesh: the editor shows and picks them alike.
const char *assetTypeString(Panda::AssetType type) {
    if (type == Panda::AssetType::TEXTURE || type == Panda::AssetType::CUBE_MAP) { return "texture"; }
    if (type == Panda::AssetType::IMPORTED_MESH || type == Panda::AssetType::RUNTIME_MESH) { return "mesh"; }
    if (const Panda::AssetKind *kind = Panda::GameContext::getExtensions().assetKinds.find(type)) {
        return kind->name.c_str();
    }
    return "text"; // NONE — a generic file
}

} // namespace Editor
