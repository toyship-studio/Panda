#include "UI/Panels/CMakePanel.hpp"

#include "UI/Widgets/BuildLogView.hpp"

#include <PandaEditorKit/EditorHost.hpp>
#include <PandaEditorKit/Widgets/Icons.hpp>
#include <PandaEditorKit/Widgets/Typography.hpp>

#include <utility>

namespace Editor {

using namespace PandaUI;

namespace {
    constexpr float ACTION_HEIGHT = 24.f; // the buttons of the bar (like Clear in the console)
    constexpr Panda::BuildTargetKind SCRIPTS = Panda::BuildTargetKind::Scripts;
} // namespace

CMakePanelView::CMakePanelView(Panda::ProjectBuilds &builds)
    : m_builds(builds) {
    addSubviews();
    setupLayout();
    setupStyle();
    m_builds.setOutput(this);
}

CMakePanelView::~CMakePanelView() {
    m_builds.setOutput(nullptr);
}

std::shared_ptr<IconButton> CMakePanelView::makeAction(const char *icon, const char *title, const char *tooltip) {
    auto button = std::make_shared<IconButton>(icon, Ic::RowSize, ACTION_HEIGHT);
    button->setCornerRadius(Radius::sm);
    if (title != nullptr) { button->setTitle(title); }
    button->setTooltip(tooltip);
    button->layout().setFlexShrink(0.f);
    return button;
}

void CMakePanelView::addSubviews() {
    /* --- The bar: what is built, then the actions by their names --- */
    auto bar = std::make_shared<Panel>();
    bar->setBackgroundColor(Color(0x00000000));
    bar->layout().setFlexDirection(FlexDirection::Row);
    bar->layout().setFlexWrap(Wrap::Wrap); // a narrow dock: the actions go to a second line
    bar->layout().setAlignItems(Align::Center);
    bar->layout().setFlexShrink(0.f);
    bar->layout().setGap(6.f);
    bar->layout().setPadding(Edge::Horizontal, 8.f);
    bar->layout().setPadding(Edge::Vertical, 4.f);

    m_config = std::make_shared<SegmentedControl>();
    m_config->setSegments({"Debug", "Release"});
    m_config->setSelectedIndex(m_builds.scriptsConfig() == Panda::BuildConfig::Release ? 1 : 0);
    m_config->setOnSelectionChanged([this](SegmentedControl &config) {
        m_builds.setScriptsConfig(config.selectedIndex() == 1 ? Panda::BuildConfig::Release : Panda::BuildConfig::Debug);
    });
    bar->addSubview(m_config);

    const auto start = [this](Panda::BuildAction action) { m_builds.start(SCRIPTS, action); };
    m_configureButton = makeAction(
        Ic::CMakeConfigure, "Configure",
        "Configure: cmake makes the build tree again. After a source file was added or the toolchain changed"
    );
    m_configureButton->setOnClick([start] { start(Panda::BuildAction::Configure); });
    bar->addSubview(m_configureButton);

    m_buildButton = makeAction(Ic::Build, "Build", "Build: compile. A tree that is not configured yet is configured first");
    m_buildButton->setOnClick([start] { start(Panda::BuildAction::Build); });
    bar->addSubview(m_buildButton);

    m_cleanButton = makeAction(Ic::Trash, "Clean", "Clean: delete the build folder. The next Build starts from nothing");
    m_cleanButton->setOnClick([start] { start(Panda::BuildAction::Clean); });
    bar->addSubview(m_cleanButton);

    m_stopButton = makeAction(Ic::Stop, "Stop", "Stop the build that runs");
    m_stopButton->setOnClick([this] { m_builds.cancel(); });
    bar->addSubview(m_stopButton);

    auto spacer = std::make_shared<Panel>();
    spacer->setBackgroundColor(Color(0x00000000));
    spacer->layout().setFlexGrow(1.f);
    bar->addSubview(spacer);

    auto toolchain = makeAction(
        Ic::Configure, "Toolchain", "The cmake, the generator and the compilers of this machine: the editor settings"
    );
    toolchain->setOnClick([] { editorHost().openSettings(Panda::SettingsScope::Editor, "TOOLCHAIN"); });
    bar->addSubview(toolchain);

    auto reveal = makeAction(Ic::Folder, nullptr, "Show the folder of the scripts in the file manager");
    reveal->setOnClick([this] { m_builds.showFolder(SCRIPTS); });
    bar->addSubview(reveal);

    auto clear = makeAction(Ic::Clear, nullptr, "Clear the output");
    clear->setOnClick([this] {
        m_log->clear();
        showOutput();
    });
    bar->addSubview(clear);
    addSubview(bar);

    // A thin line separates the bar from the output (like the dock borders).
    auto divider = std::make_shared<Panel>();
    divider->setUserInteractionEnabled(false);
    divider->layout().setHeight(Length::points(1.f));
    divider->layout().setFlexShrink(0.f);
    divider->setBackgroundColor(ThemeColor::borderFaint);
    addSubview(divider);

    // What the actions do: shown while there is no output.
    m_help = std::make_shared<Label>(
        "The C++ scripts of the project: Scripts/<name>.\nBuild compiles them and the editor loads the result. "
        "Configure runs cmake again — after a source file was added or the toolchain changed. Clean deletes the "
        "build folder; the next Build starts from nothing.\nThe game itself is exported in its own window: File, "
        "Export Game."
    );
    m_help->setFont(Typo::Caption);
    m_help->setTextRole(TextRole::Muted);
    m_help->setNumberOfLines(0);
    m_help->layout().setMargin(Edge::Horizontal, 12.f);
    m_help->layout().setMargin(Edge::Vertical, 10.f);
    m_help->layout().setFlexShrink(0.f);
    addSubview(m_help);

    m_log = std::make_shared<BuildLogView>();
    m_log->layout().setFlexGrow(1.f);
    addSubview(m_log);

    updateActions();
    showOutput();
}

void CMakePanelView::update(double deltaTime) {
    EditorView::update(deltaTime);
    // One build at a time: while the game is exported, the scripts wait, and the bar says so.
    if (m_builds.isRunning() != m_running) { updateActions(); }
}

void CMakePanelView::buildStarted() {
    m_log->clear(); // each run starts with a clean output
    m_log->setFolders({m_builds.buildFolder(SCRIPTS), m_builds.folder(SCRIPTS)});
    m_config->setSelectedIndex(m_builds.scriptsConfig() == Panda::BuildConfig::Release ? 1 : 0);
    updateActions();
    showOutput();
}

void CMakePanelView::buildOutput(const std::vector<std::string> &lines) {
    m_log->append(lines);
    showOutput();
}

void CMakePanelView::buildFinished(bool success, bool canceled) {
    if (canceled) {
        m_log->append("Canceled.", BuildLogView::LineKind::StatusFail);
    } else if (success) {
        m_log->append("Finished.", BuildLogView::LineKind::StatusOk);
    } else {
        m_log->append("Failed.", BuildLogView::LineKind::StatusFail);
    }
    updateActions();
    showOutput();
}

void CMakePanelView::updateActions() {
    m_running = m_builds.isRunning();
    m_configureButton->setEnabled(!m_running);
    m_buildButton->setEnabled(!m_running);
    m_cleanButton->setEnabled(!m_running);
    m_stopButton->setEnabled(m_running);
}

void CMakePanelView::showOutput() {
    const bool empty = m_log->lineCount() == 0;
    if (m_help->isHidden() == !empty) { return; }
    m_help->setHidden(!empty);
    m_help->layout().setDisplay(empty ? Display::Flex : Display::None);
    setNeedsLayout();
}

void CMakePanelView::setupLayout() {
    layout().setFlexDirection(FlexDirection::Column);
}

void CMakePanelView::setupStyle() {
    setBackgroundColor(ThemeColor::windowBackground);
}

} // namespace Editor
