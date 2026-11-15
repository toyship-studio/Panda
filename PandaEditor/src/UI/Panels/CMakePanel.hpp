#pragma once

#include "Project/Build/ProjectBuilds.hpp"

#include <PandaEditorKit/Widgets/EditorView.hpp>

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Editor {

class IconButton;

// VIEW: the panel of the builds of the project (Panda::ProjectBuilds), which it starts and whose
// output it shows. A bar on top: the two cmake projects as tabs (Scripts | Export), what is built
// of the one in front (Debug / Release, the export target), then the actions by their names —
// Configure, Build, Clean, Stop — and, at the right edge, the toolchain settings, the project
// folder and the clearing of the output. Below — the output of cmake, a buffer per tab; a tab
// that has nothing to show yet says what its actions do.
class CMakePanelView final : public EditorView, public Panda::ProjectBuildsOutput {
public:
    explicit CMakePanelView(Panda::ProjectBuilds &builds);
    ~CMakePanelView() override;

private:
    // Panda::ProjectBuildsOutput. kind — the tab the lines belong to.
    void buildStarted(Panda::BuildTargetKind kind) override;
    void buildOutput(Panda::BuildTargetKind kind, const std::vector<std::string> &lines) override;
    void buildFinished(Panda::BuildTargetKind kind, bool success, bool canceled) override;

    // The log line kind determines the color: command, plain output, diagnostics, final status.
    enum class LineKind { Command, Plain, Warning, Error, StatusOk, StatusFail };

    struct Line {
        std::string text;
        LineKind kind;
    };

    class OutputCell;

    static constexpr int kTargetCount = 2; // Scripts, Export — index = BuildTargetKind

    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;

    std::shared_ptr<IconButton> makeAction(const char *icon, const char *title, const char *tooltip);
    void appendLine(Panda::BuildTargetKind kind, std::string text, LineKind lineKind);
    void setActiveTab(int index);
    void syncChoices();   // the selectors show what the builds are set to build
    void updateActions(); // what can be pressed now, and the name of the IDE on its button
    void reloadOutput();
    static LineKind classify(const std::string &text);

    Panda::ProjectBuilds &m_builds;
    std::array<std::vector<Line>, kTargetCount> m_lines; // output buffer per tab
    int m_activeTab = 0;
    std::optional<int> m_runningKind; // index of the target being built (one build at a time)

    std::shared_ptr<PandaUI::SegmentedControl> m_targetTabs;
    std::shared_ptr<PandaUI::SegmentedControl> m_scriptsConfig; // Debug / Release
    std::shared_ptr<PandaUI::Combobox> m_exportTarget;          // macOS / iOS … (ExportProjectTarget)
    std::shared_ptr<IconButton> m_configureButton;
    std::shared_ptr<IconButton> m_buildButton;
    std::shared_ptr<IconButton> m_cleanButton;
    std::shared_ptr<IconButton> m_stopButton;
    std::shared_ptr<IconButton> m_openButton; // the Export project in its IDE
    std::shared_ptr<PandaUI::CollectionView> m_list;
    std::shared_ptr<PandaUI::Label> m_help; // what the actions of the tab do, while it has no output
};

} // namespace Editor
