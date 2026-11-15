#pragma once

#include "Base/AssetHandler.hpp"
#include "Panda/Renderer/ShaderSpirvReflectionData.hpp"

#include <Miren/Miren.hpp>

namespace Panda {

/// A shader program from a cooked pair of stage files. The program is created on its first use:
/// a shader nothing draws is never compiled, and a pair replaced before the first draw (an engine
/// scene shader recooked for a project) costs nothing.
class ShaderAsset : public Asset {
public:
    ShaderAsset();
    ShaderAsset(const path_t &vertexBinPath, const path_t &fragmentBinPath);
    ~ShaderAsset();
    Miren::ProgramHandle getMirenHandle();
    // Another cooked pair under the same asset: the id and every material that refers to the
    // shader stay, the next use creates the program of the new pair.
    void reload(const path_t &vertexBinPath, const path_t &fragmentBinPath);
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

private:
    path_t m_vertexBinPath; // empty — the shader has no pair (it failed to cook): nothing is drawn
    path_t m_fragmentBinPath;
    Miren::ProgramHandle m_handle;
    AssetRef<Asset> m_instancedVariant;
    AssetRef<Asset> m_shadowVariant;
};

} // namespace Panda
