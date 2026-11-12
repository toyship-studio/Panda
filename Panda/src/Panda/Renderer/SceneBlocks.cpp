#include "Panda/Renderer/SceneBlocks.hpp"
#include "Panda/Assets/StaticResources.hpp"
#include "Panda/Renderer/SceneWind.hpp"
#include "Panda/Renderer/ShaderFeatures.hpp"

#include <Miren/Miren.hpp>

namespace Panda {

namespace {

    // The system texture of a feature under the name the catalog gives it.
    void bindTexture(
        Miren::ViewId view,
        ShaderFeature feature,
        const AssetRef<TextureAsset> &texture,
        const AssetRef<TextureAsset> &placeholder
    ) {
        TextureAsset *asset = texture.get();
        Miren::addViewInputTexture(
            view,
            ShaderFeatures::info(feature).textures.front().name,
            (asset ? asset : placeholder.get())->getMirenHandle()
        );
    }

} // namespace

void SceneBlocks::assemble(const WorldLightingData &lighting) {
    m_view = ViewUbo::from(lighting.cameraPosition);
    m_lights = LightsUbo::from(lighting);
    m_shadows = ShadowsUbo::from(lighting.shadows);
    m_occlusion = OcclusionUbo::from(lighting.occlusion);
    m_environment = EnvironmentUbo::from(lighting.environment);
    m_wind = WindUbo::from(SceneWind::phaseVector());
    m_shadowAtlas = lighting.shadows.atlas;
    m_occlusionVolume = lighting.occlusion.volume;
    m_environmentMap = lighting.environment.map;
}

void SceneBlocks::bindBlocks(Miren::ViewId view) const {
    bindBlock(view, m_view);
    bindBlock(view, m_lights);
    bindBlock(view, m_shadows);
    bindBlock(view, m_occlusion);
    bindBlock(view, m_environment);
    bindBlock(view, m_wind);
}

void SceneBlocks::bind(Miren::ViewId view) const {
    bindBlocks(view);
    // Without an atlas — a depth placeholder (a depth2d slot, the shader does not reach the sample);
    // without a volume — a white one (no occlusion); without a map — the built-in sky gradient.
    bindTexture(view, ShaderFeature::Shadows, m_shadowAtlas, StaticResources::shadowFallbackTexture);
    bindTexture(view, ShaderFeature::Occlusion, m_occlusionVolume, StaticResources::occlusionFallbackTexture);
    bindTexture(view, ShaderFeature::Environment, m_environmentMap, StaticResources::defaultSkyTexture);
}

} // namespace Panda
