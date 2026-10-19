#pragma once

#include "PandaUI/Core/View.hpp"

namespace PandaUI {

// Progress indicator. Determinate: fill by the value 0..1. Indeterminate: a running segment
// (for "work is in progress, duration unknown"). The track is the view background (theme.control),
// the fill is theme.accent. The container sets the width; the height is thin by default.
class ProgressBar : public View {
public:
    ProgressBar();

    void setProgress(float progress); // 0..1, clamped; turns indeterminate off
    float getProgress() const {
        return m_progress;
    }
    void setIndeterminate(bool indeterminate);
    bool isIndeterminate() const {
        return m_indeterminate;
    }

protected:
    void update(double deltaTime) override;
    void draw(float offsetX, float offsetY) override;
    void themeChanged() override;

private:
    void applyTrackStyle();

    float m_progress = 0.f;
    bool m_indeterminate = false;
    double m_phase = 0.0; // indeterminate animation phase, [0..1)
};

} // namespace PandaUI
