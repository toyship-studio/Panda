//
// Created by Michael Andreichev on 15.09.2023.
//

#pragma once

#include "Panda/GameLogic/Components/TransformComponent.hpp"
#include "Panda/Base/Base.hpp"
#include "Panda/Assets/MaterialAsset.hpp"
#include "Panda/Assets/RuntimeMeshAsset.hpp"
#include "Panda/Assets/ImportedMeshAsset.hpp"
#include "Panda/GameLogic/Camera.hpp"
#include "Panda/Renderer/RenderingMode.hpp"
#include "Panda/Renderer/ShaderInterface/ShaderBlocks.hpp"
#include "Panda/Renderer/WorldLighting.hpp"

#include <Miren/Miren.hpp>
#include <array>
#include <vector>

namespace Panda {

struct MeshLayoutPrograms; // Extensions/ResourceProviders.hpp

// TODO: - 1. Compute the model matrix only once for static meshes
// TODO: - 2. Batch rendering

class Renderer3D final {
public:
    // Draw calls of the frame by passes: accumulated between resetStats (once per frame, World) —
    // the scene (DEFAULT) and the sum of the shadow passes (SHADOW_DEPTH); IDS/GEOMETRY_ONLY are
    // not counted.
    struct Statistics {
        uint32_t drawCalls = 0;
        uint32_t shadowDrawCalls = 0;
    };

    struct DrawData {
        Renderer3D::Statistics stats;
    };

    Renderer3D();
    Renderer3D(Renderer3D &&other);
    ~Renderer3D() = default;
    void begin(RenderingMode mode, Miren::ViewId id);
    void submitSky(glm::mat4 &skyViewProj, AssetRef<RuntimeMeshAsset> skyMesh, AssetRef<MaterialAsset> material);
    void submit(UUID id, glm::mat4 &model, AssetRef<RuntimeMeshAsset> mesh, AssetRef<MaterialAsset> material);
    // Meshes outside the asset registry (the owner holds the RuntimeMesh itself — chunks,
    // procedural geometry). A mesh with its own vertex layout (Extensions::meshLayouts) goes
    // through the programs of that layout in the IDS and depth passes.
    void submit(UUID id, glm::mat4 &model, const RuntimeMesh &mesh, AssetRef<MaterialAsset> material);
    // Instancing of an imported mesh: all primitives of all submeshes are drawn with a single
    // set of instances (a mat4 stream). Primitive material: override (if set — for the whole
    // mesh) → the slot layout of the mesh asset itself → builtin instancedLit.
    void submitInstanced(
        glm::mat4 &model,
        AssetRef<ImportedMeshAsset> mesh,
        AssetRef<MaterialAsset> overrideMaterial,
        Miren::VertexBufferHandle instanceBuffer,
        Miren::VertexLayoutHandle instanceLayout,
        uint32_t instanceCount
    );
    void submit(
        UUID id,
        glm::mat4 &model,
        AssetRef<ImportedMeshAsset> mesh,
        uint32_t submeshIndex,
        uint32_t primitiveIndex,
        AssetRef<MaterialAsset> material
    );
    // Instancing: the mesh is drawn instanceCount times with one draw call. instanceBuffer — a
    // vertex buffer with instance data laid out by instanceLayout (for matrices —
    // StaticResources::instanceMat4Layout, 4×vec4 = the columns of glm::mat4; the final transform =
    // model × instance). The material's shader must declare the instance attributes after the
    // vertex ones (fallback — instancedLitMaterial). Passes — DEFAULT and SHADOW_DEPTH: instances
    // are not picked and not highlighted per object.
    void submitInstanced(
        glm::mat4 &model,
        const RuntimeMesh &mesh,
        AssetRef<MaterialAsset> material,
        Miren::VertexBufferHandle instanceBuffer,
        Miren::VertexLayoutHandle instanceLayout,
        uint32_t instanceCount
    );
    void end();
    // Debug wireframe of the DEFAULT pass (editor viewport); does not touch highlighting/IDS.
    void setForceWireframe(bool force) {
        m_forceWireframe = force;
    }
    RenderingMode getMode() const {
        return m_mode;
    }
    Statistics getStats();
    void resetStats();
    void setViewProj(glm::mat4 viewProj);
    void setLighting(const WorldLightingData &lighting);
    // Whether the shader uses SCENE_LIGHTS (an asset flag computed on load). Reused in Renderer2D.
    static bool shaderUsesSceneLights(AssetRef<ShaderAsset> shader);
    // System lighting textures for the shaders that declare them: the shadow atlas (nullptr —
    // a depth placeholder), the crown occlusion atlas (nullptr — a white placeholder) and the
    // environment map (nullptr — the built-in sky gradient). Reused in Renderer2D and the
    // previews — the slots must always be filled.
    static void bindSystemLightTextures(
        AssetRef<ShaderAsset> shader,
        TextureAsset *shadowAtlas,
        TextureAsset *occlusionVolume,
        TextureAsset *environmentMap = nullptr
    );

    Renderer3D &operator=(Renderer3D &&other);

private:
    void flush();
    void bindSceneLightingIfNeeded(AssetRef<MaterialAsset> material);
    void countDrawCall();
    // layoutPrograms — the programs of the mesh's own vertex layout (null — the common layout).
    Miren::ProgramHandle bindShadowDepthProgram(
        const AssetRef<MaterialAsset> &material, const MeshLayoutPrograms *layoutPrograms, bool instanced
    );
    // Common tail of both submitInstanced overloads — the single source of truth of the instanced
    // draw: pass program, state, material fields on the slots of that program, lighting, UBO_VERT,
    // submit.
    void drawInstancedPrimitive(
        glm::mat4 &model,
        AssetRef<MaterialAsset> material,
        Miren::VertexBufferHandle vertexBuffer,
        Miren::IndexBufferHandle indexBuffer,
        uint32_t indicesCount,
        Miren::VertexBufferHandle instanceBuffer,
        Miren::VertexLayoutHandle instanceLayout,
        uint32_t instanceCount
    );

    DrawData m_drawData;
    RenderingMode m_mode;
    bool m_forceWireframe = false;
    Miren::ViewId m_viewId;
    glm::mat4 m_viewProj;
    WorldLightingData m_sceneLighting;
    SceneLightsUbo m_lightingUbo{}; // SCENE_LIGHTS, built in setLighting once per frame
};

} // namespace Panda
