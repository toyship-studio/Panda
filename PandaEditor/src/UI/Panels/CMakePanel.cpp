#include "UI/Panels/CMakePanel.hpp"

#include "Project/Build/ProjectBuilder.hpp"
#include "Project/Export/ExportProjectTarget.hpp"
#include <PandaEditorKit/Widgets/Icons.hpp>
#include <PandaEditorKit/Widgets/Typography.hpp>

#include <utility>

namespace Editor {

using namespace PandaUI;

namespace {
    constexpr float ROW_HEIGHT = 22.f;  // dense build log (the engine console — 26)
    constexpr float RAIL_WIDTH = 32.f;  // left mini toolbar (like VisibilityStrip)
    constexpr float RAIL_BUTTON = 24.f; // rail buttons (like Clear in the console)

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
        const Theme &t = theme();
        Font font = Typo::ValueSmall;
        Color color = t.textSecondary;
        switch (kind) {
            case LineKind::Command:
                font = font.withWeight(FontWeight::Medium);
                color = t.textPrimary;
                break;
            case LineKind::Plain: break;
            case LineKind::Warning: color = t.warning; break;
            case LineKind::Error: color = t.danger; break;
            case LineKind::StatusOk:
                font = font.withWeight(FontWeight::Medium);
                color = t.success;
                break;
            case LineKind::StatusFail:
                font = font.withWeight(FontWeight::Medium);
                color = t.danger;
                break;
        }
        m_text->setFont(font);
        m_text->setTextColor(color);
        m_text->setText(text);
    }

private:
    std::shared_ptr<Label> m_text;
};

CMakePanelView::CMakePanelView(CMakePanelOutput *output)
    : m_output(output) {
    addSubviews();
    setupLayout();
    setupStyle();
}

void CMakePanelView::addSubviews() {
    /* --- Left action rail: Configure / Build / Stop … Clear --- */
    auto rail = std::make_shared<Panel>();
    rail->setBackgroundColor(Color(0x00000000));
    rail->layout().setFlexDirection(FlexDirection::Column);
    rail->layout().setAlignItems(Align::Center);
    rail->layout().setFlexShrink(0.f);
    rail->layout().setWidth(Length::points(RAIL_WIDTH));
    rail->layout().setGap(4.f);
    rail->layout().setPadding(Edge::Vertical, 6.f);

    auto makeRailButton = [](const char *icon, const char *tooltip) {
        auto button = std::make_shared<IconButton>(icon, Ic::RowSize, RAIL_BUTTON);
        button->setCornerRadius(5.f);
        button->setTooltip(tooltip);
        return button;
    };
    // Action of the active tab: Export — the target preset, Scripts — Debug/Release.
    const auto request = [this](Panda::BuildAction action) {
        const Panda::BuildTargetKind kind = kindOfTab(m_activeTab);
        const Panda::ScriptsBuildConfig config = m_scriptsConfig->selectedIndex() == 1
                                                     ? Panda::ScriptsBuildConfig::Release
                                                     : Panda::ScriptsBuildConfig::Debug;
        if (kind == Panda::BuildTargetKind::Export) {
            m_output->cmakeDidRequestExportBuild(
                action, static_cast<Panda::ExportProjectTarget>(m_exportPreset->selectedIndex())
            );
        } else {
            m_output->cmakeDidRequestScriptsBuild(action, config);
        }
    };
    m_configureButton = makeRailButton(Ic::CMakeConfigure, "Configure");
    m_configureButton->setOnClick([request] { request(Panda::BuildAction::Configure); });
    rail->addSubview(m_configureButton);

    m_buildButton = makeRailButton(Ic::Build, "Build");
    m_buildButton->setOnClick([request] { request(Panda::BuildAction::Build); });
    rail->addSubview(m_buildButton);

    m_stopButton = makeRailButton(Ic::Stop, "Stop Build");
    m_stopButton->setEnabled(false);
    m_stopButton->setOnClick([this] { m_output->cmakeDidTapStop(); });
    rail->addSubview(m_stopButton);

    auto railSpacer = std::make_shared<Panel>();
    railSpacer->setBackgroundColor(Color(0x00000000));
    railSpacer->layout().setFlexGrow(1.f);
    rail->addSubview(railSpacer);

    // Utilities at the bottom of the rail: the active tab's project folder and clearing the output.
    auto reveal = makeRailButton(Ic::FolderOpen, "Show Project Folder");
    reveal->setOnClick([this] { m_output->cmakeDidRequestShowProjectFolder(kindOfTab(m_activeTab)); });
    rail->addSubview(reveal);

    auto clear = makeRailButton(Ic::Clear, "Clear Output");
    clear->setOnClick([this] {
        m_lines[m_activeTab].clear();
        reloadOutput();
    });
    rail->addSubview(clear);
    addSubview(rail);

    // A thin line separates the rail from the content (like the dock borders).
    auto divider = std::make_shared<Panel>();
    divider->setUserInteractionEnabled(false);
    divider->layout().setWidth(Length::points(1.f));
    divider->layout().setFlexShrink(0.f);
    divider->setThemeStyle([](View &view) { view.setBackgroundColor(theme().borderFaint); });
    addSubview(divider);

    /* --- Main column: target tabs + configuration, the output below --- */
    auto main = std::make_shared<Panel>();
    main->setBackgroundColor(Color(0x00000000));
    main->layout().setFlexDirection(FlexDirection::Column);
    main->layout().setFlexGrow(1.f);

    auto bar = std::make_shared<Panel>();
    bar->setBackgroundColor(Color(0x00000000));
    bar->layout().setFlexDirection(FlexDirection::Row);
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

    auto barSpacer = std::make_shared<Panel>();
    barSpacer->setBackgroundColor(Color(0x00000000));
    barSpacer->layout().setFlexGrow(1.f);
    bar->addSubview(barSpacer);

    m_scriptsConfig = std::make_shared<SegmentedControl>();
    m_scriptsConfig->setSegments({"Debug", "Release"});
    m_scriptsConfig->setSelectedIndex(0);
    bar->addSubview(m_scriptsConfig);

    // Item order = ExportProjectTarget values (index → enum).
    m_exportPreset = std::make_shared<Combobox>();
    m_exportPreset->setItems({"Xcode macOS", "Xcode iOS Simulator", "Xcode iOS Device", "Visual Studio Windows"});
    m_exportPreset->setSelectedIndex(0);
    m_exportPreset->layout().setWidth(Length::points(190.f));
    bar->addSubview(m_exportPreset);
    main->addSubview(bar);

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
    main->addSubview(m_list);
    addSubview(main);

    setActiveTab(0);
}

void CMakePanelView::appendLines(Panda::BuildTargetKind kind, const std::vector<std::string> &lines) {
    std::vector<Line> &buffer = m_lines[tabIndex(kind)];
    for (const std::string &text : lines) {
        buffer.push_back({text, classify(text)});
    }
    if (tabIndex(kind) == m_activeTab) { reloadOutput(); }
}

void CMakePanelView::buildStarted(Panda::BuildTargetKind kind) {
    m_runningKind = tabIndex(kind);
    m_lines[*m_runningKind].clear(); // each command starts with a clean output
    setActiveTab(*m_runningKind); // the build shows its own tab itself — the output is in view right away
    m_targetTabs->setSelectedIndex(*m_runningKind);
    updateActions();
}

void CMakePanelView::buildFinished(Panda::BuildTargetKind kind, bool success, bool canceled) {
    if (canceled) {
        appendLine(kind, "Build canceled.", LineKind::StatusFail);
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

void CMakePanelView::setActiveTab(int index) {
    m_activeTab = index;
    const bool exportTab = index == tabIndex(Panda::BuildTargetKind::Export);
    // Each tab has its own configuration selector: Debug/Release ↔ export preset.
    auto show = [](const std::shared_ptr<View> &view, bool on) {
        view->setHidden(!on);
        view->layout().setDisplay(on ? Display::Flex : Display::None);
    };
    show(m_scriptsConfig, !exportTab);
    show(m_exportPreset, exportTab);
    reloadOutput();
    setNeedsLayout();
}

void CMakePanelView::updateActions() {
    const bool running = m_runningKind.has_value();
    m_configureButton->setEnabled(!running);
    m_buildButton->setEnabled(!running);
    m_stopButton->setEnabled(running);
}

void CMakePanelView::reloadOutput() {
    m_list->reloadData();
    m_list->scrollToBottom();
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
    layout().setFlexDirection(FlexDirection::Row);
}

void CMakePanelView::setupStyle() {
    setBackgroundColor(theme().windowBackground);
    if (m_list) { m_list->reloadData(); } // the lines bind their colors from the theme
}

} // namespace Editor
