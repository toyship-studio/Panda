#include "UI/ExportWindow.hpp"

#include "App/DocumentActions.hpp"
#include "App/EditorLayer.hpp"
#include "Project/Documents/FileDocuments.hpp"
#include "Project/Export/GameExporter.hpp"
#include "Project/Loader/AssetHandlerEditor.hpp"
#include "Project/Loader/ProjectLoader.hpp"
#include "Project/Settings/ProjectSettings.hpp"
#include "Project/System/SystemTools.hpp"
#include "UI/Widgets/BuildLogView.hpp"

#include <PandaEditorKit/EditorHost.hpp>
#include <PandaEditorKit/Widgets/Icons.hpp>
#include <PandaEditorKit/Widgets/Typography.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <utility>

namespace Editor {

using namespace PandaUI;
using Panda::BuildAction;
using Panda::BuildConfig;
using Panda::BuildTargetKind;
using Panda::ExportStep;
using Panda::ProjectBuilds;

namespace {

    constexpr float CHOICES_PERIOD = 0.25f; // seconds between two looks at the documents and the disk
    constexpr float LEFT_OUT_ROW = 24.f;
    constexpr size_t NAMED_DOCUMENTS = 3; // unsaved documents a notice names before it counts the rest

    std::shared_ptr<Panel> clearPanel() {
        auto panel = std::make_shared<Panel>();
        panel->setBackgroundColor(Color(0x00000000));
        return panel;
    }

    std::shared_ptr<Panel> rowPanel(float gap) {
        auto panel = clearPanel();
        panel->layout().setFlexDirection(FlexDirection::Row);
        panel->layout().setAlignItems(Align::Center);
        panel->layout().setGap(gap);
        return panel;
    }

    std::shared_ptr<Panel> spacer() {
        auto panel = clearPanel();
        panel->layout().setFlexGrow(1.f);
        return panel;
    }

    std::shared_ptr<Button> makeButton(const char *text, ButtonStyle style, const char *id, const char *tooltip) {
        auto button = std::make_shared<Button>(text);
        button->setStyle(style);
        button->setAccessibilityId(id);
        button->setTooltip(tooltip);
        button->layout().setFlexShrink(0.f);
        return button;
    }

    std::shared_ptr<Panel> divider() {
        auto line = std::make_shared<Panel>();
        line->setUserInteractionEnabled(false);
        line->layout().setHeight(Length::points(1.f));
        line->layout().setFlexShrink(0.f);
        line->setBackgroundColor(ThemeColor::borderFaint);
        return line;
    }

    // The files of a folder, summed: what a built game takes on disk.
    uint64_t sizeOnDisk(const std::filesystem::path &path) {
        std::error_code error;
        if (std::filesystem::is_regular_file(path, error)) { return std::filesystem::file_size(path, error); }
        uint64_t bytes = 0;
        for (std::filesystem::recursive_directory_iterator it(path, error), end; !error && it != end; it.increment(error)) {
            std::error_code fileError;
            if (it->is_regular_file(fileError)) { bytes += it->file_size(fileError); }
        }
        return bytes;
    }

    const char *configName(BuildConfig config) {
        return config == BuildConfig::Debug ? "Debug" : "Release";
    }

} // namespace

/* ------------------------------ A step of the export ------------------------------ */

// One of the three steps of an export with its state: a dot of the color of the state and the
// name. To a UI test — an element "step" whose value is the state.
class ExportWindowView::StepView final : public Panel {
public:
    explicit StepView(const char *title)
        : m_title(title) {
        setBackgroundColor(Color(0x00000000));
        setUserInteractionEnabled(false);
        layout().setFlexDirection(FlexDirection::Row);
        layout().setAlignItems(Align::Center);
        layout().setGap(Spacing::sm);
        m_dot = std::make_shared<Label>("●");
        m_dot->setFont(Typo::Caption);
        addSubview(m_dot);
        m_label = std::make_shared<Label>(title);
        m_label->setFont(Typo::Body);
        addSubview(m_label);
        setState(StepState::Idle);
    }

    void setState(StepState state) {
        m_state = state;
        ThemeColor dot = ThemeColor::textDisabled;
        ThemeColor text = ThemeColor::textMuted;
        switch (state) {
            case StepState::Idle: break;
            case StepState::Running:
                dot = ThemeColor::accent;
                text = ThemeColor::textPrimary;
                break;
            case StepState::Done:
                dot = ThemeColor::success;
                text = ThemeColor::textSecondary;
                break;
            case StepState::Failed:
                dot = ThemeColor::danger;
                text = ThemeColor::textPrimary;
                break;
            case StepState::Canceled:
                dot = ThemeColor::warning;
                text = ThemeColor::textSecondary;
                break;
        }
        m_dot->setTextColor(dot);
        m_label->setTextColor(text);
    }

    void describeAccessibility(Accessibility &accessibility) const override {
        static const char *const STATES[] = {"idle", "running", "done", "failed", "canceled"};
        accessibility.role = "step";
        accessibility.label = m_title;
        accessibility.value = STATES[static_cast<int>(m_state)];
        accessibility.atomic = true;
    }

private:
    std::string m_title;
    StepState m_state = StepState::Idle;
    std::shared_ptr<Label> m_dot;
    std::shared_ptr<Label> m_label;
};

/* ------------------------------ A row of the Left Out list ------------------------------ */

class ExportWindowView::LeftOutCell final : public Panel {
public:
    LeftOutCell() {
        setBackgroundColor(Color(0x00000000));
        layout().setFlexDirection(FlexDirection::Row);
        layout().setAlignItems(Align::Center);
        layout().setGap(Spacing::lg);
        layout().setPadding(Edge::Horizontal, Spacing::xl);
        m_path = std::make_shared<Label>("");
        m_path->setFont(Typo::ValueSmall);
        m_path->setClipsToBounds(true);
        m_path->layout().setFlexGrow(1.f);
        m_path->layout().setFlexShrink(1.f);
        addSubview(m_path);
        m_detail = std::make_shared<Label>("");
        m_detail->setFont(Typo::ValueSmall);
        m_detail->setTextRole(TextRole::Dim);
        m_detail->layout().setFlexShrink(0.f);
        addSubview(m_detail);
    }

    void bind(const LeftOut &asset) {
        m_path->setText(asset.path);
        m_detail->setText(asset.kind + "  " + Panda::CookReport::sizeText(asset.bytes));
    }

private:
    std::shared_ptr<Label> m_path;
    std::shared_ptr<Label> m_detail;
};

/* ------------------------------ The window ------------------------------ */

ExportWindowView::ExportWindowView(Panda::EditorLayer &app, std::function<void()> onClose)
    : m_app(app)
    , m_builds(app.builds())
    , m_onClose(std::move(onClose))
    , m_targets(ProjectBuilds::exportTargets()) {
    setAccessibilityId("exportWindow");
    setFocusable(true); // Escape closes the window also with nothing in focus
    setBackgroundColor(ThemeColor::windowBackground);
    layout().setFlexDirection(FlexDirection::Column);
    layout().setFlexGrow(1.f);
    layout().setWidth(Length::percent(100.f));
    layout().setHeight(Length::percent(100.f));

    auto header = clearPanel();
    header->layout().setFlexDirection(FlexDirection::Column);
    header->layout().setFlexShrink(0.f);
    header->layout().setPadding(Edge::Horizontal, Spacing::xl);
    header->layout().setPadding(Edge::Top, Spacing::lg);
    header->layout().setPadding(Edge::Bottom, Spacing::md);
    header->layout().setGap(2.f);
    auto title = std::make_shared<Label>("Export Game");
    title->setFont(Typo::DialogTitle);
    header->addSubview(title);
    auto hint = std::make_shared<Label>("The game as an application, made from the saved files of the project.");
    hint->setFont(Typo::Caption);
    hint->setTextRole(TextRole::Dim);
    header->addSubview(hint);
    addSubview(header);

    auto choices = clearPanel();
    choices->layout().setFlexDirection(FlexDirection::Column);
    choices->layout().setFlexShrink(0.f);
    choices->layout().setPadding(Edge::Horizontal, Spacing::xl);
    choices->layout().setGap(Spacing::sm);
    buildChoices(*choices);
    addSubview(choices);

    // What stands in the way of an export, a line each; none — no place taken.
    m_notices = clearPanel();
    m_notices->setAccessibilityId("notices");
    m_notices->layout().setFlexDirection(FlexDirection::Column);
    m_notices->layout().setFlexShrink(0.f);
    m_notices->layout().setPadding(Edge::Horizontal, Spacing::xl);
    m_notices->layout().setPadding(Edge::Top, Spacing::md);
    m_notices->layout().setGap(Spacing::sm);
    m_notices->layout().setDisplay(Display::None);
    addSubview(m_notices);

    auto actions = clearPanel();
    actions->layout().setFlexDirection(FlexDirection::Column);
    actions->layout().setFlexShrink(0.f);
    actions->layout().setPadding(Spacing::xl);
    actions->layout().setGap(Spacing::lg);
    buildActions(*actions);
    addSubview(actions);
    addSubview(divider());

    // What came of it: the game and its pack, what the cook left out, the output of the run.
    auto tabBar = rowPanel(Spacing::lg);
    tabBar->layout().setFlexShrink(0.f);
    tabBar->layout().setPadding(Edge::Horizontal, Spacing::xl);
    tabBar->layout().setPadding(Edge::Vertical, Spacing::md);
    m_tabs = std::make_shared<SegmentedControl>();
    m_tabs->setAccessibilityId("tabs");
    m_tabs->setSegments({"Result", "Left Out", "Log"});
    m_tabs->setOnSelectionChanged([this](SegmentedControl &tabs) { showTab(static_cast<Tab>(tabs.selectedIndex())); });
    tabBar->addSubview(m_tabs);
    addSubview(tabBar);

    buildResult();
    addSubview(m_resultView);
    buildLeftOut();
    addSubview(m_leftOutView);
    m_log = std::make_shared<BuildLogView>();
    m_log->setAccessibilityId("log");
    m_log->layout().setFlexGrow(1.f);
    m_log->layout().setFlexBasis(0.f);
    addSubview(m_log);

    showTab(Tab::Result);
    readReport();
}

void ExportWindowView::buildChoices(View &parent) {
    // What: the game starts in the start world of the project and carries what it reaches. The
    // choice is a setting of the project — changed where the settings are.
    auto startRow = rowPanel(Spacing::lg);
    m_startWorld = std::make_shared<Label>("");
    m_startWorld->setAccessibilityId("startWorld");
    m_startWorld->setFont(Typo::ValueSmall);
    m_startWorld->setClipsToBounds(true);
    m_startWorld->layout().setFlexShrink(1.f);
    startRow->addSubview(m_startWorld);
    auto change = makeButton("Change", ButtonStyle::Link, "changeStartWorld", "Settings, Project, World");
    change->setSize(ButtonSize::Small);
    change->setOnClick([](Button &) { editorHost().openSettings(Panda::SettingsScope::Project, "WORLD"); });
    startRow->addSubview(change);
    parent.addSubview(std::make_shared<PropertyRow>("Start World", startRow));

    // For what. The targets are the ones this machine can build.
    m_target = std::make_shared<SegmentedControl>();
    m_target->setAccessibilityId("target");
    std::vector<std::string> names;
    int chosen = 0;
    for (size_t i = 0; i < m_targets.size(); i++) {
        names.emplace_back(ProjectBuilds::exportTargetName(m_targets[i]));
        if (m_targets[i] == m_builds.exportTarget()) { chosen = static_cast<int>(i); }
    }
    m_target->setSegments(std::move(names));
    if (!m_targets.empty()) {
        m_builds.setExportTarget(m_targets[chosen]); // a target of another machine is not kept
        m_target->setSelectedIndex(chosen);
    }
    m_target->setOnSelectionChanged([this](SegmentedControl &target) {
        const int index = target.selectedIndex();
        if (index < 0 || index >= static_cast<int>(m_targets.size())) { return; }
        m_builds.setExportTarget(m_targets[index]);
        m_shownActions = -1; // the name of the IDE on its button
        syncGame();
    });
    if (m_targets.size() > 1) { parent.addSubview(std::make_shared<PropertyRow>("Target", m_target, false)); }

    m_config = std::make_shared<SegmentedControl>();
    m_config->setAccessibilityId("configuration");
    m_config->setSegments({"Debug", "Release"});
    m_config->setSelectedIndex(m_builds.exportConfig() == BuildConfig::Release ? 1 : 0);
    m_config->setOnSelectionChanged([this](SegmentedControl &config) {
        m_builds.setExportConfig(config.selectedIndex() == 1 ? BuildConfig::Release : BuildConfig::Debug);
        syncGame();
    });
    parent.addSubview(std::make_shared<PropertyRow>("Configuration", m_config, false));

    // The name, the icon and the signing of the game are files of its Export project.
    auto applicationRow = rowPanel(Spacing::lg);
    auto application = std::make_shared<Label>("Name, icon, signing: the Export project");
    application->setFont(Typo::Caption);
    application->setTextRole(TextRole::Dim);
    applicationRow->addSubview(application);
    auto showProject = makeButton("Show Folder", ButtonStyle::Link, "showExportProject", "The Export folder of the project");
    showProject->setSize(ButtonSize::Small);
    showProject->setOnClick([this](Button &) { m_builds.showFolder(BuildTargetKind::Export); });
    applicationRow->addSubview(showProject);
    parent.addSubview(std::make_shared<PropertyRow>("Application", applicationRow));
}

void ExportWindowView::buildActions(View &parent) {
    auto buttons = rowPanel(Spacing::md);
    m_export = makeButton("Export", ButtonStyle::Accent, "export", "Cook the data, then build the game");
    m_export->setOnClick([this](Button &) { m_builds.start(BuildTargetKind::Export, BuildAction::Build); });
    buttons->addSubview(m_export);
    m_openIde = makeButton("Open in Xcode", ButtonStyle::Neutral, "openIde", "Cook the data, then open the Export project");
    m_openIde->setOnClick([this](Button &) { m_builds.openExportProject(); });
    buttons->addSubview(m_openIde);
    m_cook = makeButton("Cook Only", ButtonStyle::Neutral, "cook", "Only the data: what the game would carry");
    m_cook->setOnClick([this](Button &) { m_builds.start(BuildTargetKind::Export, BuildAction::Cook); });
    buttons->addSubview(m_cook);
    buttons->addSubview(spacer());
    m_stop = makeButton("Stop", ButtonStyle::Neutral, "stop", "Stop the export");
    m_stop->setOnClick([this](Button &button) {
        // A step stops at its next line of output, and a compiler takes its time to go.
        m_builds.cancel();
        button.setText("Stopping");
        button.setEnabled(false);
    });
    buttons->addSubview(m_stop);
    parent.addSubview(buttons);

    // How it goes: the three steps, and a bar that moves while one of them runs.
    auto steps = rowPanel(Spacing::lg);
    steps->setAccessibilityId("steps");
    static const char *const TITLES[] = {"Cook", "Configure", "Build"};
    for (size_t i = 0; i < m_steps.size(); i++) {
        m_steps[i] = std::make_shared<StepView>(TITLES[i]);
        steps->addSubview(m_steps[i]);
    }
    m_progress = std::make_shared<ProgressBar>();
    m_progress->layout().setFlexGrow(1.f);
    m_progress->setHidden(true);
    steps->addSubview(m_progress);
    parent.addSubview(steps);
}

void ExportWindowView::buildResult() {
    m_resultView = clearPanel();
    m_resultView->setAccessibilityId("result");
    m_resultView->layout().setFlexDirection(FlexDirection::Column);
    m_resultView->layout().setFlexGrow(1.f);
    m_resultView->layout().setFlexBasis(0.f);
    m_resultView->layout().setPadding(Edge::Horizontal, Spacing::xl);
    m_resultView->layout().setPadding(Edge::Bottom, Spacing::xl);
    m_resultView->layout().setGap(Spacing::lg);

    // The game: what the build made, and the two things to do with it.
    auto game = rowPanel(Spacing::md);
    game->layout().setFlexShrink(0.f);
    auto names = clearPanel();
    names->layout().setFlexDirection(FlexDirection::Column);
    names->layout().setFlexGrow(1.f);
    names->layout().setFlexShrink(1.f);
    names->layout().setGap(2.f);
    m_gameTitle = std::make_shared<Label>("");
    m_gameTitle->setAccessibilityId("game");
    m_gameTitle->setFont(Typo::Body.withWeight(FontWeight::Medium));
    names->addSubview(m_gameTitle);
    m_gameDetail = std::make_shared<Label>("");
    m_gameDetail->setFont(Typo::Caption);
    m_gameDetail->setTextRole(TextRole::Dim);
    names->addSubview(m_gameDetail);
    game->addSubview(names);
    m_reveal = makeButton("Show in Folder", ButtonStyle::Neutral, "reveal", "The built game in the file manager");
    m_reveal->setOnClick([this](Button &) {
        const ProjectBuilds::BuiltGame built = m_builds.builtGame();
        if (!built.product.empty()) { Panda::SystemTools::revealInFileManager(built.product); }
    });
    game->addSubview(m_reveal);
    m_run = makeButton("Run", ButtonStyle::Neutral, "run", "Start the built game");
    m_run->setOnClick([this](Button &) { m_builds.runBuiltGame(); });
    game->addSubview(m_run);
    m_resultView->addSubview(game);
    m_resultView->addSubview(divider());

    // Its data: where it starts, the pack, what each kind of asset takes.
    m_packText = std::make_shared<Label>("");
    m_packText->setAccessibilityId("pack");
    m_packText->setFont(Typo::ValueSmall);
    m_packText->setTextRole(TextRole::Secondary);
    m_packText->setNumberOfLines(0);
    m_resultView->addSubview(m_packText);
}

void ExportWindowView::buildLeftOut() {
    m_leftOutView = clearPanel();
    m_leftOutView->layout().setFlexDirection(FlexDirection::Column);
    m_leftOutView->layout().setFlexGrow(1.f);
    m_leftOutView->layout().setFlexBasis(0.f);
    m_leftOutView->layout().setGap(Spacing::md);

    m_leftOutText = std::make_shared<Label>("");
    m_leftOutText->setFont(Typo::Caption);
    m_leftOutText->setTextRole(TextRole::Dim);
    m_leftOutText->setNumberOfLines(0);
    m_leftOutText->layout().setFlexShrink(0.f);
    m_leftOutText->layout().setMargin(Edge::Horizontal, Spacing::xl);
    m_leftOutView->addSubview(m_leftOutText);

    m_leftOutList = std::make_shared<CollectionView>(std::make_shared<ListCollectionLayout>(LEFT_OUT_ROW));
    m_leftOutList->setAccessibilityId("leftOut");
    m_leftOutList->setSelectionMode(CollectionSelectionMode::Single);
    m_leftOutList->layout().setFlexGrow(1.f);
    CollectionViewDataSource source;
    source.numberOfItems = [this] { return static_cast<int>(m_leftOut.size()); };
    source.cellForReuse = [] { return std::make_shared<LeftOutCell>(); };
    source.configureCell = [this](View &cell, int index) { static_cast<LeftOutCell &>(cell).bind(m_leftOut[index]); };
    source.titleForItem = [this](int index) { return m_leftOut[index].path; };
    m_leftOutList->setDataSource(source);
    m_leftOutList->setOnSelectionChanged([this](CollectionView &list) {
        if (list.selectedIndices().size() == 1) { revealLeftOut(list.selectedIndices().front()); }
    });
    m_leftOutView->addSubview(m_leftOutList);
}

void ExportWindowView::contextAttached() {
    Context *context = getContext();
    if (context == nullptr) { return; }
    context->setUnhandledKeyDownHandler([this](const KeyEvent &event) {
        if (event.key != Key::Escape) { return false; }
        if (m_onClose) { m_onClose(); }
        return true;
    });
}

void ExportWindowView::update(double deltaTime) {
    Panel::update(deltaTime);
    m_sinceChoicesSync += deltaTime;
    if (m_sinceChoicesSync >= CHOICES_PERIOD) {
        m_sinceChoicesSync = 0.0;
        syncChoices();
        syncGame();
    }
    syncRun();
    if (m_builds.cookCount() != m_shownCook || !m_reportShown) { readReport(); }
}

/* ------------------------------ What is exported, and what stands in the way ------------------------------ */

void ExportWindowView::syncChoices() {
    const std::string startWorld =
        m_app.project().hasOpenedProject() ? m_app.documents().projectSettings().get<Panda::ProjectWorldSettings>().startWorld
                                            : std::string();
    if (!m_choicesShown || startWorld != m_shownStartWorld) {
        m_shownStartWorld = startWorld;
        m_startWorld->setText(startWorld.empty() ? "Not chosen" : startWorld);
        m_startWorld->setTextRole(startWorld.empty() ? TextRole::Muted : TextRole::Primary);
        m_shownActions = -1;
    }
    m_choicesShown = true;

    std::vector<std::string> notices;
    bool unsaved = false;
    if (m_targets.empty()) { notices.emplace_back("The editor exports a game on macOS and on Windows."); }
    if (startWorld.empty()) { notices.emplace_back("No start world: the game has nowhere to start."); }
    {
        const Panda::FileDocumentList files = m_app.documents().list();
        std::string names;
        size_t count = 0;
        for (Panda::FileDocument *file : files.all) {
            if (!file->isDirty()) { continue; }
            if (count < NAMED_DOCUMENTS) { names += (count > 0 ? ", " : "") + file->title(); }
            ++count;
        }
        if (count > NAMED_DOCUMENTS) { names += " and " + std::to_string(count - NAMED_DOCUMENTS) + " more"; }
        if (count > 0) {
            unsaved = true;
            notices.push_back("Not saved, so not in the game: " + names + ".");
        }
    }
    if (m_builds.isRunning() && m_builds.kind() == BuildTargetKind::Scripts) {
        notices.emplace_back("The scripts are being built: the export waits for them.");
    }
    if (notices == m_shownNotices) { return; }
    m_shownNotices = notices;
    m_notices->removeAllSubviews();
    for (const std::string &text : notices) {
        auto row = rowPanel(Spacing::md);
        row->addSubview(makeIcon(Ic::Warning, Ic::RowSize, ThemeColor::warning));
        auto label = std::make_shared<Label>(text);
        label->setFont(Typo::Caption);
        label->setTextColor(ThemeColor::warning);
        label->setNumberOfLines(0);
        label->layout().setFlexShrink(1.f);
        row->addSubview(label);
        if (unsaved && text.rfind("Not saved", 0) == 0) {
            auto save = makeButton("Save All", ButtonStyle::Link, "saveAll", "Save every document");
            save->setSize(ButtonSize::Small);
            save->setOnClick([this](Button &) { m_app.documentActions().saveAll(); });
            row->addSubview(save);
        }
        m_notices->addSubview(row);
    }
    m_notices->layout().setDisplay(notices.empty() ? Display::None : Display::Flex);
    setNeedsLayout();
}

/* ------------------------------ The run ------------------------------ */

ExportWindowView::StepState ExportWindowView::stateOf(ExportStep step) const {
    const ExportStep current = m_builds.exportStep();
    if (current == ExportStep::None) { return StepState::Idle; }
    // The run goes as far as its action: a cook alone, the project configured, the game built.
    const BuildAction action = m_builds.action();
    const ExportStep last = action == BuildAction::Cook        ? ExportStep::Cook
                            : action == BuildAction::Configure ? ExportStep::Configure
                                                               : ExportStep::Build;
    if (step > last || step > current) { return StepState::Idle; }
    if (step < current) { return StepState::Done; }
    switch (m_builds.lastResult()) {
        case ProjectBuilds::Result::Running: return StepState::Running;
        case ProjectBuilds::Result::Succeeded: return StepState::Done;
        case ProjectBuilds::Result::Failed: return StepState::Failed;
        case ProjectBuilds::Result::Canceled: return StepState::Canceled;
        case ProjectBuilds::Result::None: break;
    }
    return StepState::Idle;
}

void ExportWindowView::syncRun() {
    const bool running = m_builds.isRunning();
    const bool exportRun = m_builds.kind() == BuildTargetKind::Export;
    const bool exportRunning = running && exportRun;
    const ProjectBuilds::Result result = m_builds.lastResult();

    // The output of the last export, as it comes; a new run starts it again.
    if (exportRun) {
        const std::vector<std::string> &output = m_builds.lastOutput();
        if (output.size() < m_shownLines || (result == ProjectBuilds::Result::Running && m_statusShown)) {
            m_log->clear();
            m_shownLines = 0;
            m_statusShown = false;
        }
        if (output.size() > m_shownLines) {
            m_log->append(std::vector<std::string>(output.begin() + static_cast<std::ptrdiff_t>(m_shownLines), output.end()));
            m_shownLines = output.size();
        }
        if (!m_statusShown && result != ProjectBuilds::Result::Running && result != ProjectBuilds::Result::None) {
            m_statusShown = true;
            if (result == ProjectBuilds::Result::Succeeded) {
                m_log->append("Finished.", BuildLogView::LineKind::StatusOk);
            } else {
                m_log->append(
                    result == ProjectBuilds::Result::Canceled ? "Canceled." : "Failed.", BuildLogView::LineKind::StatusFail
                );
            }
        }
    }

    // A run that begins shows its output; one that ends well — what came of it.
    if (exportRunning && !m_exportWasRunning) { showTab(Tab::Log); }
    if (!exportRunning && m_exportWasRunning) {
        syncGame();
        if (result == ProjectBuilds::Result::Succeeded) { showTab(Tab::Result); }
    }
    m_exportWasRunning = exportRunning;

    for (size_t i = 0; i < m_steps.size(); i++) {
        m_steps[i]->setState(stateOf(static_cast<ExportStep>(static_cast<int>(ExportStep::Cook) + static_cast<int>(i))));
    }

    // What can be pressed. One build at a time: the export waits for a build of the scripts.
    const bool canStart = !running && !m_targets.empty() && !m_shownStartWorld.empty();
    const int actions = (canStart ? 1 : 0) | (exportRunning ? 2 : 0) | (static_cast<int>(m_builds.exportTarget()) << 2);
    if (actions == m_shownActions) { return; }
    m_shownActions = actions;
    m_export->setEnabled(canStart);
    m_openIde->setEnabled(canStart);
    m_cook->setEnabled(canStart);
    m_stop->setHidden(!exportRunning); // there while there is something to stop
    m_stop->layout().setDisplay(exportRunning ? Display::Flex : Display::None);
    m_stop->setText("Stop");
    m_stop->setEnabled(true);
    m_openIde->setText(std::string("Open in ") + ProjectBuilds::exportIdeName(m_builds.exportTarget()));
    m_target->setUserInteractionEnabled(!exportRunning); // a run keeps the target it began with
    m_config->setUserInteractionEnabled(!exportRunning);
    m_progress->setHidden(!exportRunning);
    m_progress->setIndeterminate(exportRunning);
    setNeedsLayout();
}

/* ------------------------------ What came of it ------------------------------ */

void ExportWindowView::syncGame() {
    const ProjectBuilds::BuiltGame game = m_builds.builtGame();
    const std::string target = m_targets.empty() ? std::string() : ProjectBuilds::exportTargetName(m_builds.exportTarget());
    const std::string what = target + ", " + configName(m_builds.exportConfig());
    std::string title, detail;
    if (game.product.empty()) {
        title = "Not built";
        detail = m_targets.empty() ? std::string() : "Export builds the game for " + what + ".";
    } else {
        // A build writes the executable again: its time tells when the size is to be summed again.
        const std::filesystem::path &stamp = game.executable.empty() ? game.product : game.executable;
        std::error_code error;
        const std::filesystem::file_time_type time = std::filesystem::last_write_time(stamp, error);
        if (game.product != m_sizedGame || time != m_sizedTime) {
            m_sizedGame = game.product;
            m_sizedTime = time;
            m_gameBytes = sizeOnDisk(game.product);
        }
        title = game.product.filename().string();
        detail = what + ", " + Panda::CookReport::sizeText(m_gameBytes);
    }
    if (title == m_gameTitle->getText() && detail == m_gameDetail->getText()) { return; }
    m_gameTitle->setText(title);
    m_gameDetail->setText(detail);
    const auto show = [](const std::shared_ptr<Button> &button, bool on) {
        button->setHidden(!on);
        button->layout().setDisplay(on ? Display::Flex : Display::None);
    };
    show(m_reveal, !game.product.empty());
    show(m_run, !game.executable.empty());
    setNeedsLayout();
}

void ExportWindowView::readReport() {
    m_shownCook = m_builds.cookCount();
    m_reportShown = true;
    m_leftOut.clear();
    Panda::CookReport report;
    if (!m_app.project().hasOpenedProject() || !m_builds.readCookReport(report)) {
        m_packText->setText("Not cooked yet. Cook Only shows what the game would carry.");
        m_leftOutText->setText("Not cooked yet.");
        m_tabs->setSegments({"Result", "Left Out", "Log"});
        m_tabs->setSelectedIndex(static_cast<int>(m_tab));
        m_leftOutList->reloadData();
        return;
    }
    std::string text = "Starts in " + report.startWorld + "\n" + std::filesystem::path(report.pack).filename().string() + ": " +
                       Panda::CookReport::sizeText(report.packBytes) + " in " + std::to_string(report.packFiles) + " files\n";
    // What each kind of asset takes in the pack; "engine" is what the engine adds.
    char line[160];
    std::snprintf(line, sizeof(line), "\n%-16s %6s   %s", "", "assets", "in the pack");
    text += line;
    for (const Panda::CookReportKind &kind : report.kinds) {
        std::snprintf(
            line, sizeof(line), "\n%-16s %6u   %s", kind.kind.c_str(), kind.assets, Panda::CookReport::sizeText(kind.bytes).c_str()
        );
        text += line;
    }
    if (!report.warnings.empty()) {
        text += "\n\nWarnings: " + std::to_string(report.warnings.size()) + ", see the Log";
    }
    m_packText->setText(text);

    for (const Panda::CookReportFile &file : report.unused) { m_leftOut.push_back({file.path, file.kind, file.bytes}); }
    if (m_leftOut.empty()) {
        m_leftOutText->setText("Every asset of the project is in the game.");
    } else {
        m_leftOutText->setText(
            std::to_string(m_leftOut.size()) + (m_leftOut.size() == 1 ? " asset, " : " assets, ") +
            Panda::CookReport::sizeText(report.unusedBytes) + ", not in the game. Select one to see it in the Content Browser."
        );
    }
    m_tabs->setSegments(
        {"Result", m_leftOut.empty() ? std::string("Left Out") : "Left Out · " + std::to_string(m_leftOut.size()), "Log"}
    );
    m_tabs->setSelectedIndex(static_cast<int>(m_tab));
    m_leftOutList->clearSelection();
    m_leftOutList->reloadData();
}

void ExportWindowView::revealLeftOut(int index) {
    Panda::AssetHandlerEditor *handler = Panda::AssetHandlerEditor::current();
    if (handler == nullptr || index < 0 || index >= static_cast<int>(m_leftOut.size())) { return; }
    const Panda::AssetId id = handler->getAssetId(handler->getProjectPath() / m_leftOut[index].path);
    if (id) { editorHost().revealAsset(id); } // gone since the cook: nothing to show
}

void ExportWindowView::showTab(Tab tab) {
    m_tab = tab;
    if (m_tabs->selectedIndex() != static_cast<int>(tab)) { m_tabs->setSelectedIndex(static_cast<int>(tab)); }
    const auto show = [](View &view, bool on) {
        view.setHidden(!on);
        view.layout().setDisplay(on ? Display::Flex : Display::None);
    };
    show(*m_resultView, tab == Tab::Result);
    show(*m_leftOutView, tab == Tab::LeftOut);
    show(*m_log, tab == Tab::Log);
    setNeedsLayout();
}

/* ------------------------------ ExportWindow ------------------------------ */

ExportWindow::ExportWindow(Panda::EditorLayer &app)
    : m_app(app) {}

ExportWindow::~ExportWindow() {
    close();
}

bool ExportWindow::show() {
    if (m_view == nullptr) { m_view = std::make_shared<ExportWindowView>(m_app, [this] { requestClose(); }); }
    if (!m_app.openAuxiliaryWindow("Export Game", PandaUI::Size(760.f, 700.f), m_view)) {
        m_view.reset();
        return false;
    }
    m_focused = true;
    m_closeRequested = false;
    return true;
}

void ExportWindow::close() {
    if (m_view == nullptr) { return; }
    m_app.closeAuxiliaryWindow();
    m_view.reset();
    m_focused = false;
    m_closeRequested = false;
}

void ExportWindow::requestClose() {
    if (m_view != nullptr) { m_closeRequested = true; }
}

bool ExportWindow::isCloseRequested() const {
    return m_closeRequested;
}

void ExportWindow::setFocused(bool focused) {
    if (m_view != nullptr) { m_focused = focused; }
}

bool ExportWindow::isFocused() const {
    return m_view != nullptr && m_focused;
}

} // namespace Editor
