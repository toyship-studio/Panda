#pragma once

#include "Preview/PreviewStage.hpp"

#include <Panda/Renderer/PostprocessingPipeline.hpp>

#include <Miren/Miren.hpp>

#include <cstdint>

namespace Panda {

// Draws a preview stage into a display texture: the stage in linear light into an HDR scratch
// target (RGBA16F and depth, like the scene) of twice the size, then the engine's Tonemap into
// the target of the caller — which also brings the picture down to its size, four samples a
// pixel. A frame has two pairs of views for that (MirenViewDistribution) — one preview of a
// form and one thumbnail — so each slot is drawn at most once a frame.
class PreviewRenderer final {
public:
    enum class Slot {
        Live,      // the preview a form shows (Views::PREVIEW_*)
        Thumbnail, // a thumbnail being built (Views::THUMBNAIL_*)
    };

    PreviewRenderer() = default;
    ~PreviewRenderer();
    PreviewRenderer(const PreviewRenderer &) = delete;
    PreviewRenderer &operator=(const PreviewRenderer &) = delete;

    // One frame of the stage through the orbit into `display` — a framebuffer over an RGBA8
    // texture of width × height pixels, transparent where the stage is empty.
    void render(
        Slot slot, PreviewStage &stage, const PreviewOrbit &orbit, Miren::FrameBufferHandle display, uint32_t width,
        uint32_t height
    );
    // The view of a slot that writes the display texture: a thumbnail of a texture is a blit
    // into it, without a stage.
    static Miren::ViewId displayView(Slot slot);

private:
    struct Scratch {
        Miren::FrameBufferHandle frameBuffer;
        Miren::TextureHandle color;
        Miren::TextureHandle depth;
        uint32_t width = 0;
        uint32_t height = 0;
    };
    void ensureScratch(Scratch &scratch, uint32_t width, uint32_t height);
    void releaseScratch(Scratch &scratch);

    Scratch m_scratch[2];               // by slot
    PostprocessingPipeline m_finalizer; // an empty chain: only the implicit Tonemap runs
};

} // namespace Panda
