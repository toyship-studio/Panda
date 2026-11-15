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
// output it shows. On the left — a vertical rail of actions (Configure / Build / Stop … Show
// Project Folder / Clear), on top — the tabs of the two cmake projects (Scripts | Export), each
// with its own choice (Debug / Release, the export preset), below — the output of cmake, a buffer
// per tab.
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

    void appendLine(Panda::BuildTargetKind kind, std::string text, LineKind lineKind);
    void setActiveTab(int index);
    void updateActions(); // enabled state of the rail buttons from m_runningKind
    void reloadOutput();
    static LineKind classify(const std::string &text);

    Panda::ProjectBuilds &m_builds;
    std::array<std::vector<Line>, kTargetCount> m_lines; // output buffer per tab
    int m_activeTab = 0;
    std::optional<int> m_runningKind; // index of the target being built (one build at a time)

    std::shared_ptr<IconButton> m_configureButton;
    std::shared_ptr<IconButton> m_buildButton;
    std::shared_ptr<IconButton> m_stopButton;
    std::shared_ptr<PandaUI::SegmentedControl> m_targetTabs;
    std::shared_ptr<PandaUI::SegmentedControl> m_scriptsConfig; // Debug / Release
    std::shared_ptr<PandaUI::Combobox> m_exportPreset;          // macOS / iOS … (ExportProjectTarget)
    std::shared_ptr<PandaUI::CollectionView> m_list;
};

} // namespace Editor
