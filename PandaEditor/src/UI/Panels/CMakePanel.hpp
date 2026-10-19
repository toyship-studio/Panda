#pragma once

#include <PandaEditorKit/Widgets/EditorView.hpp>

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Panda {
enum class BuildAction; // Project/Build/ProjectBuilder.hpp
enum class ScriptsBuildConfig;
enum class BuildTargetKind;
enum class ExportProjectTarget; // Project/Export/ExportProjectTarget.hpp
} // namespace Panda

namespace Editor {

class IconButton;

// Intents of the CMake panel → coordinator (the actual build is run by EditorLayer via EngineBridge).
class CMakePanelOutput {
public:
    virtual ~CMakePanelOutput() = default;
    virtual void cmakeDidRequestScriptsBuild(Panda::BuildAction action, Panda::ScriptsBuildConfig config) = 0;
    virtual void cmakeDidRequestExportBuild(Panda::BuildAction action, Panda::ExportProjectTarget target) = 0;
    virtual void cmakeDidTapStop() = 0;
    // Show the project folder of the ACTIVE tab in the file manager (Scripts/<name>, Export).
    virtual void cmakeDidRequestShowProjectFolder(Panda::BuildTargetKind kind) = 0;
};

// VIEW: the project build panel (CLion-like, v2). On the left — a vertical action mini toolbar
// (Configure/Build/Stop/Clear), on top — target tabs (Scripts | Export) with their own
// configuration selector, below — the cmake output log (a separate buffer per tab). Display only —
// the lines are pushed by the coordinator through Input methods, the build is run by EditorLayer.
class CMakePanelView final : public EditorView {
public:
    explicit CMakePanelView(CMakePanelOutput *output);

    // Input (from the coordinator). kind routes the output to the buffer of the corresponding tab.
    void appendLines(Panda::BuildTargetKind kind, const std::vector<std::string> &lines);
    void buildStarted(Panda::BuildTargetKind kind);
    void buildFinished(Panda::BuildTargetKind kind, bool success, bool canceled);

private:
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

    CMakePanelOutput *m_output = nullptr;
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
