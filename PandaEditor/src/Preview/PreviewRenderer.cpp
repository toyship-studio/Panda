#include "Preview/PreviewRenderer.hpp"

#include <Panda/Renderer/MirenViewDistribution.hpp>

#include <algorithm>

namespace Panda {

namespace {

    constexpr uint32_t SUPERSAMPLING = 2;       // the scratch target per side of the picture
    constexpr uint32_t MAX_SCRATCH_SIDE = 4096; // a larger picture is drawn at its own size

    Miren::ViewId stageView(PreviewRenderer::Slot slot) {
        return slot == PreviewRenderer::Slot::Live ? Views::PREVIEW_RENDER_VIEW : Views::THUMBNAIL_RENDER_VIEW;
    }

} // namespace

Miren::ViewId PreviewRenderer::displayView(Slot slot) {
    return slot == Slot::Live ? Views::PREVIEW_TONEMAP_VIEW : Views::THUMBNAIL_TONEMAP_VIEW;
}

PreviewRenderer::~PreviewRenderer() {
    for (Scratch &scratch : m_scratch) { releaseScratch(scratch); }
}

void PreviewRenderer::releaseScratch(Scratch &scratch) {
    if (!scratch.color.isValid()) { return; }
    Miren::deleteFrameBuffer(scratch.frameBuffer);
    Miren::deleteTexture(scratch.color);
    Miren::deleteTexture(scratch.depth);
    scratch = Scratch{};
}

void PreviewRenderer::ensureScratch(Scratch &scratch, uint32_t width, uint32_t height) {
    if (scratch.color.isValid() && scratch.width == width && scratch.height == height) { return; }
    releaseScratch(scratch);
    Miren::TextureCreate create;
    create.m_minFiltering = Miren::LINEAR;
    create.m_magFiltering = Miren::LINEAR;
    create.m_wrap = Miren::CLAMP;
    create.m_width = static_cast<uint16_t>(width);
    create.m_height = static_cast<uint16_t>(height);
    create.m_format = Miren::TextureFormat::RGBA16F; // linear light, like the scene
    scratch.color = Miren::createTexture(create);
    create.m_format = Miren::TextureFormat::DEPTH24STENCIL8;
    scratch.depth = Miren::createTexture(create);
    Miren::FrameBufferAttachment attachments[] = {scratch.color, scratch.depth};
    scratch.frameBuffer = Miren::createFrameBuffer(Miren::FrameBufferSpecification(attachments, 2));
    scratch.width = width;
    scratch.height = height;
}

void PreviewRenderer::render(
    Slot slot, PreviewStage &stage, const PreviewOrbit &orbit, Miren::FrameBufferHandle display, uint32_t width,
    uint32_t height
) {
    if (width == 0 || height == 0) { return; }
    // The stage is drawn at twice the size of the picture and brought down by the Tonemap pass,
    // which reads the scratch target between its texels: four samples a pixel, averaged in
    // linear light — the edges of a model are smooth without an antialiasing pass.
    const uint32_t scale = std::max(width, height) * SUPERSAMPLING <= MAX_SCRATCH_SIDE ? SUPERSAMPLING : 1;
    Scratch &scratch = m_scratch[static_cast<int>(slot)];
    ensureScratch(scratch, width * scale, height * scale);
    const Miren::ViewId stageId = stageView(slot);
    Miren::setViewport(stageId, Miren::Rect(0, 0, width * scale, height * scale));
    Miren::setViewClear(stageId, 0x00000000); // transparent: the plate of the UI shows around the subject
    Miren::setViewFrameBuffer(stageId, scratch.frameBuffer);
    stage.draw(stageId, orbit, static_cast<float>(width) / static_cast<float>(height));

    const Miren::Rect viewport(0, 0, width, height);
    const Miren::ViewId displayId = displayView(slot);
    Miren::setViewport(displayId, viewport);
    Miren::setViewClear(displayId, 0x00000000);
    Miren::setViewFrameBuffer(displayId, display);
    PostprocessingFrameInputs inputs;
    inputs.color = scratch.color;
    // One pass, the Tonemap, straight into the view of the slot: the views of the chain passes
    // belong to the viewport of the frame, and a second pass here (the antialiasing) would have
    // to go through one of them.
    inputs.tonemap.antialiasing = false;
    m_finalizer.applyToView(AssetRef<PostprocessingAsset>(), inputs, width, height, 0.f, displayId);
}

} // namespace Panda
