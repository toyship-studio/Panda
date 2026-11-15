#include "UI/Panels/CMakePanel.hpp"

#include "Project/Export/ExportProjectTarget.hpp"

#include <PandaEditorKit/EditorHost.hpp>
#include <PandaEditorKit/Widgets/Icons.hpp>
#include <PandaEditorKit/Widgets/Typography.hpp>

#include <utility>

namespace Editor {

using namespace PandaUI;

namespace {
    constexpr float ROW_HEIGHT = 22.f;    // dense build log (the engine console — 26)
    constexpr float ACTION_HEIGHT = 24.f; // the buttons of the bar (like Clear in the console)

    int tabIndex(Panda::BuildTargetKind kind) {
        return static_cast<int>(kind); // tab order = BuildTargetKind: Scripts, Export
    }
    Panda::BuildTargetKind kindOfTab(int index) {
        return static_cast<Panda::BuildTargetKind>(index);
    }
} // namespace

// A cmake output line: monospace text, color by line kind. Reused (virtualization).
class CMakePanelView::OutputCell final : public Panel {
public:
    OutputCell()
        : m_text(std::make_shared<Label>("")) {
        setBackgroundColor(Color(0x00000000));
        layout().setFlexDirection(FlexDirection::Row);
        layout().setAlignItems(Align::Center);
        layout().setPadding(Edge::Horizontal, 10.f);
        m_text->setFont(Typo::ValueSmall);
        m_text->setClipsToBounds(true);
        m_text->layout().setFlexGrow(1.f);
        m_text->layout().setFlexShrink(1.f);
        addSubview(m_text);
    }

    void bind(const std::string &text, LineKind kind) {
        Font font = Typo::ValueSmall;
        ThemeColor color = ThemeColor::textSecondary;
        switch (kind) {
            case LineKind::Command:
                font = font.withWeight(FontWeight::Medium);
                color = ThemeColor::textPrimary;
                break;
            case LineKind::Plain: break;
            case LineKind::Warning: color = ThemeColor::warning; break;
            case LineKind::Error: color = ThemeColor::danger; break;
            case LineKind::StatusOk:
                font = font.withWeight(FontWeight::Medium);
                color = ThemeColor::success;
                break;
            case LineKind::StatusFail:
                font = font.withWeight(FontWeight::Medium);
                color = ThemeColor::danger;
                break;
        }
        m_text->setFont(font);
        m_text->setTextColor(color);
        m_text->setText(text);
    }

private:
    std::shared_ptr<Label> m_text;
};

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

    m_targetTabs = std::make_shared<SegmentedControl>();
    m_targetTabs->setSegments({"Scripts", "Export"});
    m_targetTabs->setSelectedIndex(0);
    m_targetTabs->setOnSelectionChanged([this](SegmentedControl &tabs) { setActiveTab(tabs.selectedIndex()); });
    bar->addSubview(m_targetTabs);

    m_scriptsConfig = std::make_shared<SegmentedControl>();
    m_scriptsConfig->setSegments({"Debug", "Release"});
    m_scriptsConfig->setOnSelectionChanged([this](SegmentedControl &config) {
        m_builds.setScriptsConfig(
            config.selectedIndex() == 1 ? Panda::ScriptsBuildConfig::Release : Panda::ScriptsBuildConfig::Debug
        );
    });
    bar->addSubview(m_scriptsConfig);

    // Item order = ExportProjectTarget values (index → enum).
    m_exportTarget = std::make_shared<Combobox>();
    m_exportTarget->setItems({"Xcode macOS", "Xcode iOS Simulator", "Xcode iOS Device", "Visual Studio Windows"});
    m_exportTarget->layout().setWidth(Length::points(190.f));
    m_exportTarget->setOnSelectionChanged([this](Combobox &target) {
        m_builds.setExportTarget(static_cast<Panda::ExportProjectTarget>(target.selectedIndex()));
        updateActions();
    });
    bar->addSubview(m_exportTarget);

    const auto start = [this](Panda::BuildAction action) { m_builds.start(kindOfTab(m_activeTab), action); };
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

    // The Export project in its IDE: cooked, configured and opened.
    m_openButton = makeAction(Ic::FolderOpen, "Open in Xcode", "Cook the data of the game, configure the project and open it");
    m_openButton->setOnClick([this] { m_builds.openExportProject(m_builds.exportTarget()); });
    bar->addSubview(m_openButton);

    auto spacer = std::make_shared<Panel>();
    spacer->setBackgroundColor(Color(0x00000000));
    spacer->layout().setFlexGrow(1.f);
    bar->addSubview(spacer);

    auto toolchain = makeAction(
        Ic::Configure, "Toolchain", "The cmake, the generator and the compilers of this machine: the editor settings"
    );
    toolchain->setOnClick([] { editorHost().openSettings(Panda::SettingsScope::Editor, "TOOLCHAIN"); });
    bar->addSubview(toolchain);

    auto reveal = makeAction(Ic::Folder, nullptr, "Show the folder of the project in the file manager");
    reveal->setOnClick([this] { m_builds.showFolder(kindOfTab(m_activeTab)); });
    bar->addSubview(reveal);

    auto clear = makeAction(Ic::Clear, nullptr, "Clear the output");
    clear->setOnClick([this] {
        m_lines[m_activeTab].clear();
        reloadOutput();
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

    // What the actions of the tab do: shown while the tab has no output.
    m_help = std::make_shared<Label>("");
    m_help->setFont(Typo::Caption);
    m_help->setTextRole(TextRole::Muted);
    m_help->setNumberOfLines(0);
    m_help->layout().setMargin(Edge::Horizontal, 12.f);
    m_help->layout().setMargin(Edge::Vertical, 10.f);
    m_help->layout().setFlexShrink(0.f);
    addSubview(m_help);

    // A virtualized list of output lines of the active tab (auto-scroll on append).
    m_list = std::make_shared<CollectionView>(std::make_shared<ListCollectionLayout>(ROW_HEIGHT));
    m_list->setSelectionMode(CollectionSelectionMode::None);
    m_list->layout().setFlexGrow(1.f);

    CollectionViewDataSource source;
    source.numberOfItems = [this] { return static_cast<int>(m_lines[m_activeTab].size()); };
    source.cellForReuse = [] { return std::make_shared<OutputCell>(); };
    source.configureCell = [this](View &cell, int index) {
        const Line &line = m_lines[m_activeTab][index];
        static_cast<OutputCell &>(cell).bind(line.text, line.kind);
    };
    m_list->setDataSource(source);
    addSubview(m_list);

    syncChoices();
    setActiveTab(0);
}

void CMakePanelView::buildOutput(Panda::BuildTargetKind kind, const std::vector<std::string> &lines) {
    std::vector<Line> &buffer = m_lines[tabIndex(kind)];
    for (const std::string &text : lines) {
        buffer.push_back({text, classify(text)});
    }
    if (tabIndex(kind) == m_activeTab) { reloadOutput(); }
}

void CMakePanelView::buildStarted(Panda::BuildTargetKind kind) {
    m_runningKind = tabIndex(kind);
    m_lines[*m_runningKind].clear(); // each run starts with a clean output
    syncChoices();                   // a command may have chosen the target itself (Export Game → Open Xcode)
    m_targetTabs->setSelectedIndex(*m_runningKind);
    setActiveTab(*m_runningKind); // the run shows its own tab: its output is in view right away
}

void CMakePanelView::buildFinished(Panda::BuildTargetKind kind, bool success, bool canceled) {
    if (canceled) {
        appendLine(kind, "Canceled.", LineKind::StatusFail);
    } else if (success) {
        appendLine(kind, "Finished.", LineKind::StatusOk);
    } else {
        appendLine(kind, "Failed.", LineKind::StatusFail);
    }
    m_runningKind.reset();
    updateActions();
}

void CMakePanelView::appendLine(Panda::BuildTargetKind kind, std::string text, LineKind lineKind) {
    m_lines[tabIndex(kind)].push_back({std::move(text), lineKind});
    if (tabIndex(kind) == m_activeTab) { reloadOutput(); }
}

void CMakePanelView::syncChoices() {
    m_scriptsConfig->setSelectedIndex(m_builds.scriptsConfig() == Panda::ScriptsBuildConfig::Release ? 1 : 0);
    m_exportTarget->setSelectedIndex(static_cast<int>(m_builds.exportTarget()));
}

void CMakePanelView::setActiveTab(int index) {
    m_activeTab = index;
    updateActions();
    reloadOutput();
    setNeedsLayout();
}

void CMakePanelView::updateActions() {
    const bool exportTab = m_activeTab == tabIndex(Panda::BuildTargetKind::Export);
    const auto show = [](const std::shared_ptr<View> &view, bool on) {
        view->setHidden(!on);
        view->layout().setDisplay(on ? Display::Flex : Display::None);
    };
    // Each tab has its own choice of what is built: Debug / Release ↔ the export target.
    show(m_scriptsConfig, !exportTab);
    show(m_exportTarget, exportTab);
    show(m_openButton, exportTab);
    m_openButton->setTitle(
        m_builds.exportTarget() == Panda::ExportProjectTarget::VisualStudioWindows ? "Open in Visual Studio"
                                                                                  : "Open in Xcode"
    );
    const bool running = m_runningKind.has_value();
    m_configureButton->setEnabled(!running);
    m_buildButton->setEnabled(!running);
    m_cleanButton->setEnabled(!running);
    m_openButton->setEnabled(!running);
    m_stopButton->setEnabled(running);
}

void CMakePanelView::reloadOutput() {
    // A tab with nothing to show says what its actions are for.
    const bool empty = m_lines[m_activeTab].empty();
    m_help->setText(
        m_activeTab == tabIndex(Panda::BuildTargetKind::Export)
            ? "The game as an application: the Export folder of the project, an Xcode or Visual Studio project made "
              "from the cooked data of the game.\nConfigure cooks the data and generates the project of the chosen "
              "target, Build compiles it, Open opens it in the IDE. Clean deletes the build folder of the target."
            : "The C++ scripts of the project: Scripts/<name>.\nBuild compiles them and the editor loads the result. "
              "Configure runs cmake again — after a source file was added or the toolchain changed. Clean deletes "
              "the build folder; the next Build starts from nothing."
    );
    m_help->setHidden(!empty);
    m_help->layout().setDisplay(empty ? Display::Flex : Display::None);
    m_list->reloadData();
    m_list->scrollToBottom();
    setNeedsLayout();
}

CMakePanelView::LineKind CMakePanelView::classify(const std::string &text) {
    if (text.rfind("> ", 0) == 0) { return LineKind::Command; }
    if (text.find("error") != std::string::npos || text.find("Error") != std::string::npos) {
        return LineKind::Error;
    }
    if (text.find("warning") != std::string::npos || text.find("Warning") != std::string::npos) {
        return LineKind::Warning;
    }
    return LineKind::Plain;
}

void CMakePanelView::setupLayout() {
    layout().setFlexDirection(FlexDirection::Column);
}

void CMakePanelView::setupStyle() {
    setBackgroundColor(ThemeColor::windowBackground);
}

} // namespace Editor
