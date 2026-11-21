#pragma once

#include <cstdint>

namespace Editor {
class EditorController;
}

namespace Panda {

class EditorUI;
class EditorViewports;

// What the Statistics and Profiler panels and the status of the visibility strip show: sampled
// as the frames go, handed to the panels at the pace a person reads.
class EditorStatistics final {
public:
    // Every frame, with or without a project: the status of the strip — the RAM and the VRAM of
    // the process, the load of the CPU (a share of a core, like top) and of the GPU (its frame
    // time over the wall-clock one). The metrics are cheap, but running numbers are hard to read:
    // they change twice a second. shown — the status is on in the APPEARANCE settings
    // (EditorAppearanceSettings::showSystemStatus); off, nothing is sampled.
    void updateStatus(double deltaTime, Editor::EditorController *controller, bool shown);
    // With a project open, after the frame of the world is drawn: the counters of the frame, the
    // layout passes of the chrome UI per second, the profiler four times a second.
    void updateFrame(double deltaTime, EditorViewports &viewports, EditorUI &ui, Editor::EditorController &controller);

private:
    // The layout passes of the chrome UI over a one-second window: the panel itself dirties the
    // layout by changing text (a background of two or three a second), so a per-frame value would
    // flicker; a pathology shows up as about as many passes as frames.
    uint64_t m_lastUiLayoutCount = 0;
    uint32_t m_uiLayoutAccum = 0;
    uint32_t m_uiLayoutsPerSecond = 0;
    float m_uiLayoutWindow = 0.f;
    float m_profilerRefreshWindow = 0.f;
    double m_statusTimer = 0.0;
    int m_statusFrames = 0; // frames since the status was last written
    bool m_statusShown = false; // the strip shows a status written here
};

} // namespace Panda
