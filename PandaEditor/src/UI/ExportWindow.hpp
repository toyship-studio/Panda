#pragma once

#include "Project/Build/ProjectBuilds.hpp"

#include <PandaUI/PandaUI.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Panda {
class EditorLayer;
} // namespace Panda

namespace Editor {

class BuildLogView;

// The content of the Export window (a second window of the editor, like Settings): the export of
// the game from the first choice to the game itself, top to bottom in the order of doing.
// What is exported — the start world of the project; for what — the target and the configuration;
// what stands in the way — unsaved documents, no start world; the action — Export (the cook,
// cmake, the build), or only a part of it; how it goes — the three steps and the output; what
// came of it — the game (Show, Run), what its pack holds, what the cook left out of it.
//
// The window keeps nothing of its own: every frame it shows what the builds (Panda::ProjectBuilds)
// and the files say — the run and its output, the built game on disk, report.json of the last
// cook. So it can be closed while an export runs and opened again, also in another session.
class ExportWindowView final : public PandaUI::Panel {
public:
    ExportWindowView(Panda::EditorLayer &app, std::function<void()> onClose);

    void update(double deltaTime) override;

protected:
    void contextAttached() override; // Escape closes the window whatever has the keyboard

private:
    class StepView;
    class LeftOutCell;
    enum class Tab { Result, LeftOut, Log };
    enum class StepState { Idle, Running, Done, Failed, Canceled };
    // An asset of the project the last cook left out of the game (CookReport::unused).
    struct LeftOut {
        std::string path; // in the project
        std::string kind;
        uint64_t bytes = 0;
    };

    void buildChoices(PandaUI::View &parent);
    void buildActions(PandaUI::View &parent);
    void buildResult();
    void buildLeftOut();

    void syncChoices(); // the start world and what stands in the way (a few times a second)
    void syncRun();     // the run of the builds: the actions, the steps, the output
    void syncGame();    // the built game on disk
    void readReport();  // report.json of the last cook → the Result and Left Out tabs
    void showTab(Tab tab);
    void revealLeftOut(int index); // the asset of a row, in the Content Browser
    StepState stateOf(Panda::ExportStep step) const;

    Panda::EditorLayer &m_app;
    Panda::ProjectBuilds &m_builds;
    std::function<void()> m_onClose;
    std::vector<Panda::ExportProjectTarget> m_targets; // of this machine

    // What is shown, to tell when it changed.
    double m_sinceChoicesSync = 1.0; // more than the period: the first frame shows the choices
    bool m_choicesShown = false;
    std::string m_shownStartWorld;
    std::vector<std::string> m_shownNotices;
    int m_shownActions = -1; // what can be pressed, packed: -1 — nothing shown yet
    bool m_exportWasRunning = false;
    size_t m_shownLines = 0;
    bool m_statusShown = false; // the last line of the output says how the run ended
    uint32_t m_shownCook = 0;
    bool m_reportShown = false;
    // The size of the built game on disk, summed again when the game is built again.
    std::filesystem::path m_sizedGame;
    std::filesystem::file_time_type m_sizedTime{};
    uint64_t m_gameBytes = 0;
    Tab m_tab = Tab::Result;
    std::vector<LeftOut> m_leftOut; // as the Left Out tab lists them

    std::shared_ptr<PandaUI::Label> m_startWorld;
    std::shared_ptr<PandaUI::SegmentedControl> m_target;
    std::shared_ptr<PandaUI::SegmentedControl> m_config;
    std::shared_ptr<PandaUI::Panel> m_notices;
    std::shared_ptr<PandaUI::Button> m_export;
    std::shared_ptr<PandaUI::Button> m_openIde;
    std::shared_ptr<PandaUI::Button> m_cook;
    std::shared_ptr<PandaUI::Button> m_stop;
    std::array<std::shared_ptr<StepView>, 3> m_steps; // Cook, Configure, Build
    std::shared_ptr<PandaUI::ProgressBar> m_progress;
    std::shared_ptr<PandaUI::SegmentedControl> m_tabs;
    std::shared_ptr<PandaUI::Panel> m_resultView;
    std::shared_ptr<PandaUI::Label> m_gameTitle;
    std::shared_ptr<PandaUI::Label> m_gameDetail;
    std::shared_ptr<PandaUI::Button> m_reveal;
    std::shared_ptr<PandaUI::Button> m_run;
    std::shared_ptr<PandaUI::Label> m_packText;
    std::shared_ptr<PandaUI::Panel> m_leftOutView;
    std::shared_ptr<PandaUI::Label> m_leftOutText;
    std::shared_ptr<PandaUI::CollectionView> m_leftOutList;
    std::shared_ptr<BuildLogView> m_log;
};

// The Export window: a second window of the editor over the main one. The window itself is the
// application's (EditorLayer::openAuxiliaryWindow); its content is made here and lives while it
// is open. One second window at a time: the Settings and the Export share the place
// (EditorController).
class ExportWindow final {
public:
    explicit ExportWindow(Panda::EditorLayer &app);
    ~ExportWindow();

    bool show(); // opened, or brought forward. false — the window could not be made
    void close();
    // Escape arrives from inside the key dispatch of the window's own UI, which closing destroys:
    // the owner closes the window on the next frame.
    void requestClose();
    bool isCloseRequested() const;
    bool isOpen() const {
        return m_view != nullptr;
    }
    // The window got or lost the keyboard: while it has it, Close Tab closes the window.
    void setFocused(bool focused);
    bool isFocused() const;

private:
    Panda::EditorLayer &m_app;
    std::shared_ptr<ExportWindowView> m_view; // while the window is open
    bool m_focused = false;
    bool m_closeRequested = false;
};

} // namespace Editor
