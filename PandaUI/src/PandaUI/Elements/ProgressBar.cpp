#include "PandaUI/Elements/ProgressBar.hpp"

#include "PandaUI/Core/Theme.hpp"
#include "PandaUI/Rendering/UIRenderer.hpp"
#include "PandaUI/Runtime/Context.hpp"

#include <algorithm>

namespace PandaUI {

namespace {
    constexpr float BAR_HEIGHT = 6.f;
    constexpr float INDETERMINATE_SEGMENT = 0.35f; // fraction of the width taken by the running segment
    constexpr double INDETERMINATE_PERIOD = 1.1;   // seconds per pass from left to right
}

ProgressBar::ProgressBar() {
    layout().setHeight(Length::points(BAR_HEIGHT));
    applyTrackStyle();
}

void ProgressBar::setProgress(float progress) {
    const float clamped = std::clamp(progress, 0.f, 1.f);
    const bool changed = (clamped != m_progress) || m_indeterminate;
    m_progress = clamped;
    m_indeterminate = false;
    if (changed) { setNeedsDisplay(); }
}

void ProgressBar::setIndeterminate(bool indeterminate) {
    if (m_indeterminate == indeterminate) { return; }
    m_indeterminate = indeterminate;
    m_phase = 0.0;
    setNeedsDisplay();
}

void ProgressBar::update(double deltaTime) {
    if (!m_indeterminate) { return; }
    // Animation: the 0..1 phase is looped. The frame is marked dirty only while the bar is indeterminate
    // and visible — no frames are spent on a determinate/hidden bar.
    m_phase += deltaTime / INDETERMINATE_PERIOD;
    if (m_phase >= 1.0) { m_phase -= 1.0; }
    setNeedsDisplay();
}

void ProgressBar::draw(float offsetX, float offsetY) {
    Context *context = getContext();
    if (!context) { return; }
    UIRenderer &renderer = context->getRenderer();

    const Rect frame = getFrame();
    const float w = frame.size.width;
    const float h = frame.size.height;
    if (w <= 0.f || h <= 0.f) { return; }

    const float radius = h * 0.5f;
    const float baseX = offsetX + frame.origin.x;
    const float baseY = offsetY + frame.origin.y;

    // The track is drawn by the surface (background + rounding). Here — only the fill on top.
    float fillX = 0.f;
    float fillW = 0.f;
    if (m_indeterminate) {
        // The leading edge (head) travels from 0 to w+segment; the tail lags behind by segment. Both
        // are clamped to [0,w]: the segment smoothly "grows" from the left edge at the start of the cycle
        // and "drains away" past the right edge at the end — without abruptly appearing at full width.
        const float segment = w * INDETERMINATE_SEGMENT;
        const float head = static_cast<float>(m_phase) * (w + segment);
        const float right = std::min(w, head);
        const float left = std::max(0.f, head - segment);
        fillX = left;
        fillW = std::max(0.f, right - left);
    } else {
        fillW = w * m_progress;
    }
    if (fillW <= 0.f) { return; }

    UIRenderer::RectData fill;
    fill.center = {baseX + fillX + fillW * 0.5f, baseY + h * 0.5f, 0.f};
    fill.size = {fillW, h};
    fill.color = theme().accent;
    renderer.drawRoundedRect(fill, std::min(radius, fillW * 0.5f));
}

void ProgressBar::themeChanged() {
    applyTrackStyle();
    setNeedsDisplay();
}

void ProgressBar::applyTrackStyle() {
    setBackgroundColor(theme().control);
    surface().setCornerRadius(BAR_HEIGHT * 0.5f);
}

} // namespace PandaUI
