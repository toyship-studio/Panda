#include "App/EditorStatistics.hpp"

#include "App/SystemStats.hpp"
#include "UI/EditorController.hpp"
#include "UI/Host/EditorUI.hpp"
#include "Viewport/EditorViewports.hpp"

#include <Miren/Miren.hpp>
#include <Panda/Application/Application.hpp>

#include <algorithm>
#include <cstdio>

namespace Panda {

void EditorStatistics::updateStatus(double deltaTime, Editor::EditorController *controller, bool shown) {
    if (!shown) {
        // Off in the settings: nothing is sampled, and the strip lets go of what it showed.
        if (m_statusShown && controller != nullptr) { controller->setStripStatusText(std::string()); }
        m_statusShown = false;
        m_statusTimer = 0.0;
        m_statusFrames = 0;
        return;
    }
    m_statusFrames++;
    m_statusTimer += deltaTime;
    if (m_statusTimer < 0.5) { return; }
    const double interval = m_statusTimer;
    m_statusTimer = 0.0;
    if (controller == nullptr) { return; }

    const Editor::SystemStats system = Editor::sampleSystemStats();
    const Miren::GpuStats gpu = Miren::getGpuStats();
    const double frameMs = interval * 1000.0 / std::max(1, m_statusFrames);
    const double gpuPercent = frameMs > 0.01 ? std::clamp(gpu.frameMs / frameMs * 100.0, 0.0, 999.0) : 0.0;
    m_statusFrames = 0;

    const auto gigabytes = [](uint64_t bytes) { return (double)bytes / (1024.0 * 1024.0 * 1024.0); };
    char text[128];
    snprintf(
        text, sizeof(text), "MEM %.1fG   VRAM %.1fG   CPU %.0f%%   GPU %.0f%% %.1f ms",
        gigabytes(system.residentBytes), gigabytes(gpu.allocatedBytes), system.cpuPercent, gpuPercent, gpu.frameMs
    );
    controller->setStripStatusText(text);
    m_statusShown = true;
}

void EditorStatistics::updateFrame(
    double deltaTime, EditorViewports &viewports, EditorUI &ui, Editor::EditorController &controller
) {
    // The current world has rendered the frame already (Edit or Play): the counters are of now.
    const Renderer2D::Statistics stats2d = viewports.statistics2D();
    const Renderer3D::Statistics stats3d = viewports.statistics3D();
    const uint64_t uiLayoutCount = ui.context().layoutPassCount();
    m_uiLayoutAccum += static_cast<uint32_t>(uiLayoutCount - m_lastUiLayoutCount);
    m_lastUiLayoutCount = uiLayoutCount;
    m_uiLayoutWindow += static_cast<float>(deltaTime);
    if (m_uiLayoutWindow >= 1.f) {
        m_uiLayoutsPerSecond = m_uiLayoutAccum;
        m_uiLayoutAccum = 0;
        m_uiLayoutWindow -= 1.f;
    }
    controller.setFrameStatistics(
        Application::get()->fps, stats3d.drawCalls, stats3d.shadowDrawCalls, stats2d.drawCalls, stats2d.quadCount,
        m_uiLayoutsPerSecond
    );
    controller.collectExtensionStatistics(); // contribution sections (flora, etc.)
    m_profilerRefreshWindow += static_cast<float>(deltaTime);
    if (m_profilerRefreshWindow >= 0.25f) {
        m_profilerRefreshWindow = 0.f;
        controller.updateProfilerPanel();
    }
}

} // namespace Panda
