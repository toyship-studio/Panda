#pragma once

#include <algorithm>

namespace PandaUI {

// A simple time-based tween of a single float with ease-out. A control holds it, advances it in
// update() (called every tick) and, while advance() returns true, applies value() + marks itself
// dirty. No separate animation engine is required — this is reused by various controls
// (Toggle thumb, button press, fade etc.).
class Animation {
public:
    void start(float from, float to, float duration) {
        if (duration <= 0.f) {
            m_value = to;
            m_active = false;
            return;
        }
        m_from = from;
        m_to = to;
        m_duration = duration;
        m_elapsed = 0.f;
        m_value = from;
        m_active = true;
    }

    // Set the value instantly without animation.
    void reset(float value) {
        m_value = value;
        m_elapsed = 0.f;
        m_active = false;
    }

    // Advance by deltaTime. Returns true on ticks when the animation is active (including
    // the final one, where the value is brought up to to), otherwise false.
    bool advance(double deltaTime) {
        if (!m_active) { return false; }
        m_elapsed += static_cast<float>(std::max(0.0, deltaTime));
        const float t = std::clamp(m_elapsed / m_duration, 0.f, 1.f);
        const float eased = 1.f - (1.f - t) * (1.f - t) * (1.f - t); // easeOutCubic
        m_value = m_from + (m_to - m_from) * eased;
        if (t >= 1.f) {
            m_value = m_to;
            m_active = false;
        }
        return true;
    }

    bool isActive() const {
        return m_active;
    }
    float value() const {
        return m_value;
    }

private:
    float m_from = 0.f;
    float m_to = 0.f;
    float m_value = 0.f;
    float m_elapsed = 0.f;
    float m_duration = 0.f;
    bool m_active = false;
};

} // namespace PandaUI
