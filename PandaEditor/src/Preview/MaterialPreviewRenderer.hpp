#pragma once

#include <Panda/Assets/MaterialAsset.hpp>
#include <Panda/Renderer/Renderer3D.hpp>
#include <Panda/Renderer/PostprocessingPipeline.hpp>
#include <Panda/Renderer/Vertex.hpp>
#include <Miren/Miren.hpp>

#include <vector>

namespace Panda {

// Isolated material preview rendering: a UV sphere with this material, fixed camera and studio
// light. A material on a contribution shader (EditorExtensions::materialPreviews) is drawn as a
// sphere in its vertex format and with its stub inputs — the per-rule spheres are built
// lazily. HDR: the sphere is rendered into a linear 16F scratch target
// (Views::PREVIEW_RENDER_VIEW), the display texture is produced by finalization — the pipeline's
// implicit Tonemap (Views::PREVIEW_TONEMAP_VIEW).
// The renderer has NO display texture of its own: the caller provides the target
// (ThumbnailsManager — its own texture for each live preview and for each thumbnail); the
// scratch target and views are shared.
class MaterialPreviewRenderer final {
public:
    static constexpr uint32_t SIZE = 256; // side of the square preview, px

    MaterialPreviewRenderer();
    ~MaterialPreviewRenderer();

    // A live preview frame of SIZE×SIZE into displayTarget (an FBO with the caller's RGBA8
    // texture). There is one set of preview views per editor — no more than one call per frame.
    // false — the material is not ready (no asset/shader): the target is NOT touched, it keeps
    // the previous frame of the same preview.
    bool render(AssetRef<MaterialAsset> material, Miren::FrameBufferHandle displayTarget);
    // Submits the same sphere into a foreign view (thumbnails: the caller sets up FBO/viewport).
    bool renderToView(AssetRef<MaterialAsset> material, Miren::ViewId viewId);
    // Finalization of the HDR scratch target into a display texture: a tonemap quad into viewId
    // (FBO/viewport — set by the caller).
    void finalizeToView(Miren::TextureHandle hdrColor, uint32_t size, Miren::ViewId viewId);

private:
    void createSphereMesh();

    // HDR scratch target (RGBA16F): the sphere is rendered here; display target ← Tonemap ← scratch.
    Miren::FrameBufferHandle m_hdrFrameBuffer;
    Miren::FrameBufferSpecification m_hdrFbSpecification;
    Miren::TextureHandle m_hdrColorAttachment;
    Miren::TextureHandle m_depthAttachment;
    PostprocessingPipeline m_finalizer; // empty chain → only the implicit Tonemap runs
    // Sphere of a contribution rule (rule index in the registry): built for the first material
    // under that rule.
    Miren::VertexBufferHandle sphereForRule(size_t rule);

    Miren::VertexBufferHandle m_vertexBuffer; // shared sphere (Vertex)
    std::vector<Panda::Vertex> m_sphereVertices; // for the spheres of contribution rules
    std::vector<Miren::VertexBufferHandle> m_ruleVertexBuffers; // by rule index
    Miren::IndexBufferHandle m_indexBuffer;
    uint32_t m_indicesCount = 0;
    glm::mat4 m_viewProj;
    WorldLightingData m_lighting;
};

} // namespace Panda
