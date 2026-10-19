#pragma once

#include "Miren/Base.hpp"
#include <Fern/GraphicsContext/GraphicsContext.hpp>
#include <vector>

namespace Miren {

struct View {
    View()
        : m_clearColor(0x000000ff)
        , m_clearEnabled(true)
        , m_clearDepthOverride(false)
        , m_clearDepthEnabled(true)
        , m_clearDepth(1.f)
        , m_viewport(Rect::zero())
        , m_graphicsContext(nullptr)
        , m_frameBuffer(MIREN_INVALID_HANDLE)
        , m_clearAttachments() {}

    // Whether depth is cleared on entering the view: by default — together with color
    // (m_clearEnabled), setViewClearDepth sets it separately (the cascade shadow atlas).
    bool clearsDepth() const {
        return m_clearDepthOverride ? m_clearDepthEnabled : m_clearEnabled;
    }

    uint32_t m_clearColor;
    // false — the view draws ON TOP of the target contents (for example, UI after the tonemap):
    // entering it does not erase color and depth.
    bool m_clearEnabled;
    bool m_clearDepthOverride;
    bool m_clearDepthEnabled;
    float m_clearDepth;
    Rect m_viewport;
    Fern::GraphicsContext *m_graphicsContext;
    FrameBufferHandle m_frameBuffer;
    std::vector<Clear> m_clearAttachments;
};

} // namespace Miren
