#pragma once

#include <Miren/Miren.hpp>

#include <cstdint>

namespace Panda {

// The target of a fullscreen pass of the frame (fog, exposure, bloom, a pass of a postprocessing
// chain, ambient occlusion): an HDR color texture — RGBA16F, linear light, sampled bilinearly,
// clamped at the edges — with its framebuffer. Made at the size of the first pass into it and
// resized when a pass asks for another size; the texture keeps its handle.
class PassTarget final {
public:
    PassTarget() = default;
    ~PassTarget();
    PassTarget(const PassTarget &) = delete;
    PassTarget &operator=(const PassTarget &) = delete;

    // The next view of the frame being built: a pass over the whole target at this size, which
    // it is cleared to `clearColor` on entering.
    Miren::ViewId addView(uint32_t width, uint32_t height, uint32_t clearColor = 0x000000ff);
    // What the last pass into the target left (invalid before the first one).
    Miren::TextureHandle color() const {
        return m_color;
    }

private:
    Miren::TextureHandle m_color;
    Miren::FrameBufferHandle m_frameBuffer;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
};

} // namespace Panda
