#pragma once

#include "Preview/PreviewStage.hpp"

#include <Panda/Renderer/WorldView.hpp>

#include <Miren/Miren.hpp>

#include <cstdint>

namespace Panda {

// Draws a preview stage into a display texture: the world of the stage through a WorldView of
// twice the size of the picture — the same HDR frame a viewport has — and the Tonemap of that
// view into the target of the caller, which also brings the picture down to its size, four
// samples a pixel. The renderer has a view for the preview of a form and one for the thumbnails
// (their sizes differ; the stages of a kind share its view), so each kind is drawn at most once
// a frame.
class PreviewRenderer final {
public:
    enum class Slot {
        Live,      // the preview a form shows
        Thumbnail, // a thumbnail being built
    };

    PreviewRenderer();
    PreviewRenderer(const PreviewRenderer &) = delete;
    PreviewRenderer &operator=(const PreviewRenderer &) = delete;

    // One frame of the stage through the orbit into `display` — a framebuffer over an RGBA8
    // texture of width × height pixels, transparent where the stage is empty.
    void render(
        Slot slot, PreviewStage &stage, const PreviewOrbit &orbit, Miren::FrameBufferHandle display, uint32_t width,
        uint32_t height
    );
    // The frames that were drawn are on their way to the GPU: the views let go of the assets of
    // their stages (a model does not stay loaded for the sake of the picture taken of it).
    void forgetFrames();

private:
    WorldView m_views[2]; // by slot
};

} // namespace Panda
