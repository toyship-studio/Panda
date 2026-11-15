#pragma once

#include "Miren/Base.hpp"
#include <Fern/GraphicsContext/GraphicsContext.hpp>
#include <cstdint>
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

    // The view as the frame keeps it, from its public description.
    explicit View(const ViewDescription &description)
        : m_clearColor(description.clearColor)
        , m_clearEnabled(description.clearsColor)
        , m_clearDepthOverride(true)
        , m_clearDepthEnabled(description.clearsDepth)
        , m_clearDepth(description.clearDepth)
        , m_viewport(description.viewport)
        , m_graphicsContext(description.graphicsContext)
        , m_frameBuffer(description.frameBuffer)
        , m_clearAttachments(description.clearAttachments) {}

    // Whether depth is cleared on entering the view.
    bool clearsDepth() const {
        return m_clearDepthOverride ? m_clearDepthEnabled : m_clearEnabled;
    }
    // Entering the view changes its target also when nothing is drawn into it.
    bool clearsAnything() const {
        return m_clearEnabled || clearsDepth() || !m_clearAttachments.empty();
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
