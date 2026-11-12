#pragma once

#include "Panda/Assets/Base/AssetRef.hpp"
#include "Panda/Assets/TextureAsset.hpp"
#include "Panda/Renderer/ShaderInterface/ShaderBlocks.hpp"
#include "Panda/Renderer/WorldLighting.hpp"

#include <Miren/Base.hpp>

namespace Panda {

// The scene blocks of a frame (ShaderBlocks.hpp): the camera and the block of every shader
// feature, assembled once from the frame lighting and bound once per frame to the view the scene
// is drawn into (Miren view inputs) — the renderers, the material preview, the impostor bake.
// Every draw of that view reads them, whoever submits it. Every block and system texture is bound
// always: a feature the project has off arrives as the data of an empty scene (the lighting
// system and the scene wind produce it), so a program compiled with the feature draws what a
// program compiled without it does, and a program that declares none of it ignores the bind —
// Miren resolves inputs by name.
class SceneBlocks final {
public:
    void assemble(const WorldLightingData &lighting);
    // The blocks and the system textures (a placeholder for a texture the frame has none of — the
    // slot of a declared texture must always be filled).
    void bind(Miren::ViewId view) const;
    // The blocks alone: the shadow depth passes, where the shadow atlas is the render target.
    void bindBlocks(Miren::ViewId view) const;

private:
    ViewUbo m_view{};
    LightsUbo m_lights{};
    ShadowsUbo m_shadows{};
    OcclusionUbo m_occlusion{};
    EnvironmentUbo m_environment{};
    WindUbo m_wind{};
    AssetRef<TextureAsset> m_shadowAtlas;
    AssetRef<TextureAsset> m_occlusionVolume;
    AssetRef<TextureAsset> m_environmentMap;
};

} // namespace Panda
