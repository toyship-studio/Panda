#pragma once

#include "PandaUI/Core/Appearance.hpp"
#include "PandaUI/Core/View.hpp"

namespace PandaUI {

// The look of a progress bar: the track and the fill, their images, the height and the radius.
struct ProgressBarAppearance {
    ThemeColor track = ThemeColor::control;
    ThemeColor fill = ThemeColor::accent;
    SurfaceImage trackImage;
    SurfaceImage fillImage;
    float height = 6.f;
    float cornerRadius = -1.f;         // < 0: a pill, half the height
    float indeterminateSegment = 0.35f; // the fraction of the width the running segment takes
    double indeterminatePeriod = 1.1;   // seconds per pass from left to right
};

// Determinate (0..1) or indeterminate progress.
class ProgressBar : public View {
public:
    ProgressBar();

    // The look of the bars created from now on (Appearance.hpp).
    static ProgressBarAppearance &defaultAppearance();
    void setAppearance(const ProgressBarAppearance &appearance);
    const ProgressBarAppearance &appearance() const {
        return m_appearance;
    }

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
    void applyAppearance();
    float radiusFor(float height) const;

    ProgressBarAppearance m_appearance;
    float m_progress = 0.f;
    bool m_indeterminate = false;
    double m_phase = 0.0; // indeterminate animation phase, [0..1)
};

} // namespace PandaUI
