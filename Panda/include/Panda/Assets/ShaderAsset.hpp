//
// Created by Andreichev Mikhail on 06.11.2024.
//

#pragma once

#include "Base/AssetHandler.hpp"
#include "Panda/Renderer/ShaderSpirvReflectionData.hpp"

#include <Miren/Miren.hpp>

namespace Panda {

/// Takes only compiled binaries as inputs
class ShaderAsset : public Asset {
public:
    ShaderAsset();
    ShaderAsset(const path_t &vertexBinPath, const path_t &fragmentBinPath);
    ShaderAsset(ShaderAsset &&other);
    ~ShaderAsset();
    Miren::ProgramHandle getMirenHandle();
    // Whether the shader declares the SCENE_LIGHTS UBO (the renderer binds lights only to such
    // shaders). Without reflection (binary runtime shaders) — true: the backend ignores a
    // redundant bind.
    bool usesSceneLights() const {
        return m_usesSceneLights;
    }
    // Whether it declares shadowMap (cascade shadow atlas, comparison sampler) — same contract.
    bool usesShadowMap() const {
        return m_usesShadowMap;
    }
    // Whether it declares occlusionVolume (occlusion volume atlas) — same contract.
    bool usesOcclusionVolume() const {
        return m_usesOcclusionVolume;
    }
    // Whether it declares environmentMap (world environment map: sky, reflections) — same contract.
    bool usesEnvironmentMap() const {
        return m_usesEnvironmentMap;
    }
    void setSystemInputs(
        bool sceneLights, bool shadowMap, bool occlusionVolume = false, bool environmentMap = false
    ) {
        m_usesSceneLights = sceneLights;
        m_usesShadowMap = shadowMap;
        m_usesOcclusionVolume = occlusionVolume;
        m_usesEnvironmentMap = environmentMap;
    }
    // Instanced variant of the template (shared fragment shader, vertex shader with instance
    // attributes) — a property of the shader ITSELF, like variants in Unity/UE: the renderer
    // knows no specific templates. The ref is the base Asset: AssetRef<ShaderAsset> inside its
    // own definition is still an incomplete type (pass it via asBaseAsset()).
    void setInstancedVariant(AssetRef<Asset> variant) {
        m_instancedVariant = variant;
    }
    // Program for an instanced draw: the variant, and without it — its own (a custom shader
    // must be instancing-compatible itself).
    Miren::ProgramHandle getInstancedMirenHandle() {
        ShaderAsset *variant = static_cast<ShaderAsset *>(m_instancedVariant.get());
        return variant ? variant->getMirenHandle() : getMirenHandle();
    }
    // Depth variant for shadow cascades — for shaders with non-standard vertex-shader geometry
    // (an impostor billboard expands the quad in the vertex shader): the builtin shadow_depth
    // cannot reproduce such geometry. If set, the renderer draws the caster with it (with the
    // material fields); if not set — the regular builtin path by layout/mask.
    void setShadowVariant(AssetRef<Asset> variant) {
        m_shadowVariant = variant;
    }
    Miren::ProgramHandle getShadowMirenHandle() {
        ShaderAsset *variant = static_cast<ShaderAsset *>(m_shadowVariant.get());
        return variant ? variant->getMirenHandle() : Miren::ProgramHandle();
    }
#ifdef PND_EDITOR
    virtual ShaderSpirvReflectionData getReflectionData();
#endif

protected:
    void create(const path_t &vertexBinPath, const path_t &fragmentBinPath);

    bool m_usesSceneLights = true; // a derived class with reflection refines it once on load
    bool m_usesShadowMap = false;
    bool m_usesOcclusionVolume = false;
    bool m_usesEnvironmentMap = false;

private:
    Miren::ProgramHandle m_handle;
    AssetRef<Asset> m_instancedVariant;
    AssetRef<Asset> m_shadowVariant;
};

} // namespace Panda
