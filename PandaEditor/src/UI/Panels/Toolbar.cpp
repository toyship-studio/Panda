#include "UI/Panels/Toolbar.hpp"

#include <memory>

namespace Editor {

using namespace PandaUI;

namespace {
    constexpr float TOOLBAR_HEIGHT = 42.f; // design.md §3
    constexpr float ACTION_SIZE = 28.f;    // toolbar icon buttons
    constexpr float TRANSPORT_W = 30.f;
    constexpr float TRANSPORT_H = 28.f;

    // Vertical 1×20 divider between icon groups.
    std::shared_ptr<Panel> makeDivider() {
        auto divider = std::make_shared<Panel>();
        divider->setUserInteractionEnabled(false);
        divider->layout().setSize({1.f, 20.f});
        divider->layout().setMargin(Edge::Horizontal, 4.f);
        divider->setThemeStyle([](View &view) { view.setBackgroundColor(theme().border); });
        return divider;
    }
} // namespace

ToolbarPanelView::ToolbarPanelView(ToolbarOutput *output)
    : m_output(output) {
    addSubviews();
    setupLayout();
    setupStyle();
}

void ToolbarPanelView::addSubviews() {
    // LEFT: actions (undo/redo | reload/save/export). flexBasis 0 + flexGrow 1 on the left and right
    // zones: both split the WHOLE width equally (not just the free space) — the capsule is strictly
    // centered regardless of the content on the left.
    auto left = std::make_shared<Panel>();
    left->setBackgroundColor(Color(0x00000000));
    left->layout().setFlexDirection(FlexDirection::Row);
    left->layout().setAlignItems(Align::Center);
    left->layout().setGap(6.f);
    left->layout().setFlexGrow(1.f);
    left->layout().setFlexBasis(0.f);

    // Selection history (editor tabs): back/forward, like in a browser. Restoring a closed
    // tab is also "back": the history stores objects, the tab is recreated by the selection.
    m_back = std::make_shared<IconButton>(Ic::ChevronLeft, Ic::ToolbarSize, ACTION_SIZE);
    m_back->setTooltip("Back (selection history)");
    m_back->setOnClick([this] { m_output->toolbarDidNavigateBack(); });
    m_back->setEnabled(false); // there is no history at startup
    left->addSubview(m_back);

    m_forward = std::make_shared<IconButton>(Ic::ChevronRight, Ic::ToolbarSize, ACTION_SIZE);
    m_forward->setTooltip("Forward (selection history)");
    m_forward->setOnClick([this] { m_output->toolbarDidNavigateForward(); });
    m_forward->setEnabled(false);
    left->addSubview(m_forward);

    left->addSubview(makeDivider());

    m_undo = std::make_shared<IconButton>(Ic::Undo, Ic::ToolbarSize, ACTION_SIZE);
    m_undo->setTooltip("Undo");
    m_undo->setOnClick([this] { m_output->toolbarDidUndo(); });
    left->addSubview(m_undo);

    m_redo = std::make_shared<IconButton>(Ic::Redo, Ic::ToolbarSize, ACTION_SIZE);
    m_redo->setTooltip("Redo");
    m_redo->setOnClick([this] { m_output->toolbarDidRedo(); });
    left->addSubview(m_redo);

    left->addSubview(makeDivider());

    m_reload = std::make_shared<IconButton>(Ic::Reload, Ic::ToolbarSize, ACTION_SIZE);
    m_reload->setTooltip("Reload");
    m_reload->setOnClick([this] { m_output->toolbarDidTapReload(); });
    left->addSubview(m_reload);

    auto save = std::make_shared<IconButton>(Ic::Save, Ic::ToolbarSize, ACTION_SIZE);
    save->setTooltip("Save World");
    save->setOnClick([this] { m_output->toolbarDidSave(); });
    left->addSubview(save);

    auto exportButton = std::make_shared<IconButton>(Ic::Export, Ic::ToolbarSize, ACTION_SIZE);
    exportButton->setTooltip("Export / Cook");
    exportButton->setOnClick([this] { m_output->toolbarDidExport(); });
    left->addSubview(exportButton);

    addSubview(left);

    // CENTER: the Play / Simulate / Stop transport capsule (recessed track, colored icons).
    auto capsule = std::make_shared<Panel>();
    capsule->setThemeStyle([](View &view) {
        view.setBackgroundColor(theme().inputDeep);
        view.surface().setBorderColor(theme().borderMed);
    });
    capsule->layout().setFlexDirection(FlexDirection::Row);
    capsule->layout().setAlignItems(Align::Center);
    capsule->layout().setGap(2.f);
    capsule->layout().setPadding(3.f);
    capsule->surface().setCornerRadius(9.f);
    capsule->surface().setBorderWidth(1.f);

    // fixed: the theme color of the icon (success, danger), re-read on a theme change; nullptr — neutral.
    auto makeTransport = [](const char *icon, Color Theme::*fixed) {
        auto button = std::make_shared<IconButton>(icon, Ic::ToolbarSize, TRANSPORT_W);
        button->setButtonSize(TRANSPORT_W, TRANSPORT_H);
        if (fixed) {
            button->setThemeStyle([fixed](View &view) { static_cast<IconButton &>(view).setFixedColor(theme().*fixed); });
        }
        return button;
    };

    m_play = makeTransport(Ic::Play, &Theme::success);
    m_play->setTooltip("Play");
    m_play->setOnClick([this] { m_output->toolbarDidPickSceneState(SceneState::Play); });
    capsule->addSubview(m_play);

    m_simulate = makeTransport(Ic::Simulate, nullptr); // neutral (muted → hover primary)
    m_simulate->setTooltip("Simulate");
    m_simulate->setOnClick([this] { m_output->toolbarDidPickSceneState(SceneState::Simulate); });
    capsule->addSubview(m_simulate);

    m_stop = makeTransport(Ic::Stop, &Theme::danger);
    m_stop->setTooltip("Stop");
    m_stop->setOnClick([this] { m_output->toolbarDidPickSceneState(SceneState::Edit); });
    capsule->addSubview(m_stop);

    addSubview(capsule);

    // RIGHT: the same basis 0 + grow 1 pair as on the left, with the update indicator at its end.
    auto right = std::make_shared<Panel>();
    right->setBackgroundColor(Color(0x00000000));
    right->layout().setFlexDirection(FlexDirection::Row);
    right->layout().setAlignItems(Align::Center);
    right->layout().setFlexGrow(1.f);
    right->layout().setFlexBasis(0.f);
    auto filler = std::make_shared<Panel>();
    filler->setBackgroundColor(Color(0x00000000));
    filler->layout().setFlexGrow(1.f);
    right->addSubview(filler);
    m_update = std::make_shared<UpdateIndicatorView>(this);
    m_update->layout().setMargin(Edge::Horizontal, 8.f);
    right->addSubview(m_update);
    addSubview(right);
}

void ToolbarPanelView::setUpdateStatus(const UpdateIndicatorModel &model) {
    if (m_update) { m_update->setModel(model); }
}

void ToolbarPanelView::updateIndicatorDidActivate() {
    m_output->toolbarDidActivateUpdate();
}

void ToolbarPanelView::updateIndicatorDidDismiss() {
    m_output->toolbarDidDismissUpdate();
}

void ToolbarPanelView::setCanNavigate(bool back, bool forward) {
    if (m_back) { m_back->setEnabled(back); }
    if (m_forward) { m_forward->setEnabled(forward); }
}

void ToolbarPanelView::setCanUndo(bool canUndo) {
    if (m_undo) { m_undo->setEnabled(canUndo); }
}

void ToolbarPanelView::setCanRedo(bool canRedo) {
    if (m_redo) { m_redo->setEnabled(canRedo); }
}

void ToolbarPanelView::setCanPlay(bool canPlay) {
    if (m_play) { m_play->setEnabled(canPlay); }
    if (m_simulate) { m_simulate->setEnabled(canPlay); }
}

void ToolbarPanelView::setCanStop(bool canStop) {
    if (m_stop) { m_stop->setEnabled(canStop); }
}

void ToolbarPanelView::setCanReloadScripts(bool canReload) {
    if (m_reload) { m_reload->setEnabled(canReload); }
}

void ToolbarPanelView::setupLayout() {
    layout().setFlexDirection(FlexDirection::Row);
    layout().setAlignItems(Align::Center);
    layout().setGap(6.f);
    layout().setHeight(Length::points(TOOLBAR_HEIGHT));
    layout().setFlexShrink(0.f); // the toolbar has a fixed height
    layout().setPadding(Edge::Horizontal, 10.f);
}

void ToolbarPanelView::setupStyle() {
    setBackgroundColor(theme().surface); // bg/toolbar — a separate strip above the content
}

} // namespace Editor
