//
// Created by Andreichev Mikhail on 08.06.2025.
//

#pragma once

#include <Panda/Base/Base.hpp>
#include <Miren/Miren.hpp>
#include <unordered_set>

namespace Panda {

class Viewport {
public:
    Viewport();
    ~Viewport();
    void initWithSize(Vec2 size);
    void updateSize(Size size);
    void updateOrigin(Vec2 origin);
    Miren::TextureHandle getResultTexture();
    Miren::TextureHandle getSceneColorTexture();    // scene color attachment (postprocessing input)
    Miren::TextureHandle getSceneEmissiveTexture(); // emissive buffer (bloom input)
    Miren::TextureHandle getSceneDepthTexture();    // scene depth (DoF/fog input)
    Miren::FrameBufferHandle getIdsFrameBuffer();
    Rect getFrame();
    void drawOutline(float dt, const std::unordered_set<UUID> &selection);
    // The frame after the postprocessing chain: outline reads it instead of the raw scene (invalid —
    // the raw one).
    void setPostprocessedColor(Miren::TextureHandle texture) {
        m_postprocessedColor = texture;
    }

private:
    Rect m_frame;
    Miren::TextureHandle m_postprocessedColor;
    // SCENE RENDERING:
    Miren::FrameBufferHandle m_sceneFB;
    Miren::FrameBufferSpecification m_sceneFbSpecification;
    Miren::TextureHandle m_colorAttachment;
    Miren::TextureHandle m_emissiveAttachment;
    Miren::TextureHandle m_depthAttachment;

    // SELECTED GEOMETRY ISOLATION RENDERING:
    Miren::FrameBufferHandle m_selectedGeometryFB;
    Miren::TextureHandle m_selectedGeometryAttachment;
    Miren::FrameBufferSpecification m_selectedFbSpecification;

    // ID BUFFER RENDERING:
    Miren::FrameBufferHandle m_idsFB;
    Miren::FrameBufferSpecification m_idsFbSpecification;

    // SELECTED OBJECT HIGHLIGHT OUTLINE RENDERING:
    Miren::VertexBufferHandle m_vertexBuffer;
    Miren::IndexBufferHandle m_indexBuffer;
    Miren::ProgramHandle m_outlineProgram;
    Miren::FrameBufferHandle m_outputFB;
    Miren::TextureHandle m_resultAttachment;
    Miren::FrameBufferSpecification m_outputFbSpecification;
};

} // namespace Panda
