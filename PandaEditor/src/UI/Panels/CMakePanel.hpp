#pragma once

#include "Project/Build/ProjectBuilds.hpp"

#include <PandaEditorKit/Widgets/EditorView.hpp>

#include <memory>
#include <string>
#include <vector>

namespace Editor {

class BuildLogView;
class IconButton;

// VIEW: the panel of the build of the scripts of the project (Panda::ProjectBuilds), which it
// starts and whose output it shows. A bar on top: the configuration (Debug / Release), then the
// actions by their names — Configure, Build, Clean, Stop — and, at the right edge, the toolchain
// settings, the folder of the scripts and the clearing of the output. Below — the output of
// cmake; while there is none, a line says what the actions do. The game is exported in a window
// of its own (ExportWindow).
class CMakePanelView final : public EditorView, public Panda::ScriptsBuildOutput {
public:
    explicit CMakePanelView(Panda::ProjectBuilds &builds);
    ~CMakePanelView() override;

    void update(double deltaTime) override; // the actions follow whether a build runs (an export too)

private:
    // Panda::ScriptsBuildOutput.
    void buildStarted() override;
    void buildOutput(const std::vector<std::string> &lines) override;
    void buildFinished(bool success, bool canceled) override;

    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;

    std::shared_ptr<IconButton> makeAction(const char *icon, const char *title, const char *tooltip);
    void updateActions(); // what can be pressed now
    void showOutput();    // the output, or the line about the actions while there is none

    Panda::ProjectBuilds &m_builds;
    bool m_running = false; // a build runs: one at a time, whichever project it is of

    std::shared_ptr<PandaUI::SegmentedControl> m_config; // Debug / Release
    std::shared_ptr<IconButton> m_configureButton;
    std::shared_ptr<IconButton> m_buildButton;
    std::shared_ptr<IconButton> m_cleanButton;
    std::shared_ptr<IconButton> m_stopButton;
    std::shared_ptr<BuildLogView> m_log;
    std::shared_ptr<PandaUI::Label> m_help; // what the actions do, while there is no output
};

} // namespace Editor
