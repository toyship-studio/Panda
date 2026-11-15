#include "Preview/PreviewRenderer.hpp"

#include <algorithm>

namespace Panda {

namespace {

    constexpr uint32_t SUPERSAMPLING = 2;     // the frame of the view per side of the picture
    constexpr uint32_t MAX_FRAME_SIDE = 4096; // a larger picture is drawn at its own size

} // namespace

PreviewRenderer::PreviewRenderer() {
    // Transparent: the plate of the UI shows around the subject.
    for (WorldView &view : m_views) { view.setClearColor(0x00000000); }
}

void PreviewRenderer::forgetFrames() {
    for (WorldView &view : m_views) { view.forgetFrame(); }
}

void PreviewRenderer::render(
    Slot slot, PreviewStage &stage, const PreviewOrbit &orbit, Miren::FrameBufferHandle display, uint32_t width,
    uint32_t height
) {
    if (width == 0 || height == 0) { return; }
    // The stage is drawn at twice the size of the picture and brought down by the Tonemap pass,
    // which reads the frame between its texels: four samples a pixel, averaged in linear light —
    // the edges of a model are smooth without an antialiasing pass.
    const uint32_t scale = std::max(width, height) * SUPERSAMPLING <= MAX_FRAME_SIDE ? SUPERSAMPLING : 1;
    WorldView &view = m_views[static_cast<int>(slot)];
    view.resize(width * scale, height * scale);
    stage.draw(view, orbit, static_cast<float>(width) / static_cast<float>(height));

    // The look of a picture of an asset: the Tonemap alone, straight into the display texture.
    PostprocessingFrameInputs inputs = view.postprocessingInputs(stage.world());
    inputs.tonemap.antialiasing = false;
    view.developInto(
        AssetRef<PostprocessingAsset>(), inputs, 0.f,
        {
            .frameBuffer = display,
            .viewport = Miren::Rect(0, 0, static_cast<int>(width), static_cast<int>(height)),
            .clearColor = 0x00000000,
        }
    );
}

} // namespace Panda
