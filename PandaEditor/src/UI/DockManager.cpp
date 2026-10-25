#include "UI/DockManager.hpp"

#include <PandaEditorKit/Widgets/Icons.hpp>
#include "UI/VisibilityStrip.hpp"
#include "PandaUI/Elements/Menu.hpp"
#include "PandaUI/Elements/Popup.hpp"
#include "PandaUI/Elements/TabView.hpp"
#include "PandaUI/Runtime/Context.hpp"

#include <algorithm>
#include <utility>

namespace Editor {

using namespace PandaUI;

namespace {
    constexpr float MAXIMIZE_MARGIN = 12.f; // inset of the zoom overlay from the window edges (px)

    void stretchAbsolute(const std::shared_ptr<View> &view) {
        view->layout().setPositionType(PositionType::Absolute);
        view->layout().setPosition(Edge::Left, Length::points(0.f));
        view->layout().setPosition(Edge::Top, Length::points(0.f));
        view->layout().setPosition(Edge::Right, Length::points(0.f));
        view->layout().setPosition(Edge::Bottom, Length::points(0.f));
    }
} // namespace

DockManager::DockManager(std::shared_ptr<View> toolbar, std::shared_ptr<TabView> center, std::vector<DockPanel> panels)
    : m_center(std::move(center))
    , m_panels(std::move(panels)) {
    // A stable tab-id per panel (1..N in specification order) — survives a dock rebuild so that
    // tab move/reorder find the panel by id. Transient panels continue the counter.
    for (std::size_t i = 0; i < m_panels.size(); ++i) { m_panelTabId[m_panels[i].id] = i + 1; }
    m_nextTabId = m_panels.size() + 1;

    m_leftDock = std::make_shared<TabView>();
    m_rightDock = std::make_shared<TabView>();
    m_bottomDock = std::make_shared<TabView>();
    // All docks always show their tab bar — the tab is the panel title (no separate panel header),
    // and the tab bar carries «+»/fullscreen/close consistently.
    m_leftDock->setOnTabClosed([this](TabView &, int idx) { panelClosed(Dock::Left, idx); });
    m_rightDock->setOnTabClosed([this](TabView &, int idx) { panelClosed(Dock::Right, idx); });
    m_bottomDock->setOnTabClosed([this](TabView &, int idx) { panelClosed(Dock::Bottom, idx); });
    m_center->setOnTabClosed([this](TabView &, int idx) { panelClosed(Dock::Center, idx); });

    // Drag-and-drop of tabs between docks (one group): reorder within the own bar + move between bars.
    const Dock allDocks[] = {Dock::Left, Dock::Right, Dock::Bottom, Dock::Center};
    for (Dock d : allDocks) {
        const auto tv = dockTabView(d);
        tv->setTabDragGroup("docks");
        tv->setOnReorder([this, d](TabView &, int, int to) { tabReordered(d, to); });
        tv->setOnTabMoved([this, d](TabView &, std::uint64_t id, int idx) { tabMovedToDock(d, id, idx); });
        tv->setOnTabContextMenu([this, d](TabView &, int index, Point position) { showTabMenu(d, index, position); });
        tv->setOnTabDoubleClicked([this, d](TabView &, int index) { tabDoubleClicked(d, index); });
        // Close button gate: a transient with unsaved changes may cancel the close.
        tv->setOnTabCloseRequest([this, d](TabView &view, int index) {
            return allowClose(panelForTabId(view.tabId(index)));
        });
        // Active tab change (a click on a tab) → update the icon highlight in the strip.
        tv->setOnSelectionChanged([this](TabView &) {
            if (!m_inRebuild) { refreshStrip(); }
        });
    }

    m_leftDock->setTrailingAccessory(makeDockTrailing(Dock::Left));
    m_rightDock->setTrailingAccessory(makeDockTrailing(Dock::Right));
    m_bottomDock->setTrailingAccessory(makeDockTrailing(Dock::Bottom));
    m_center->setTrailingAccessory(makeDockTrailing(Dock::Center));

    // The center (viewport) is above the bottom dock; the work area is left dock | center | right dock.
    m_centerColumn = std::make_shared<SplitView>(SplitOrientation::Vertical);
    m_centerColumn->addPane(m_center, 3.f);
    m_centerColumn->addPane(m_bottomDock, 2.f);

    m_workspace = std::make_shared<SplitView>(SplitOrientation::Horizontal);
    m_workspace->addPane(m_leftDock, 1.f);
    m_workspace->addPane(m_centerColumn, 3.2f);
    m_workspace->addPane(m_rightDock, 1.6f);

    // Zoom overlay: a dim over the whole window + an inset frame (the zoomed zone is moved there). Hidden.
    m_maximizeRoot = std::make_shared<Panel>();
    m_maximizeRoot->setBackgroundColor(Color(0x00000000));
    stretchAbsolute(m_maximizeRoot);
    m_maximizeRoot->layout().setDisplay(Display::None);

    auto backdrop = std::make_shared<Panel>();
    backdrop->setBackgroundColor(Color(0x00000099));
    stretchAbsolute(backdrop);
    m_maximizeRoot->addSubview(backdrop);

    m_maximizeHost = std::make_shared<Panel>();
    m_maximizeHost->setBackgroundColor(ThemeColor::windowBackground);
    m_maximizeHost->setClipsToBounds(true);
    m_maximizeHost->surface().setCornerRadius(Radius::lg);
    m_maximizeHost->surface().setBorderWidth(1.f);
    m_maximizeHost->surface().setBorderColor(ThemeColor::border);
    m_maximizeHost->layout().setPositionType(PositionType::Absolute);
    m_maximizeHost->layout().setPosition(Edge::Left, Length::points(MAXIMIZE_MARGIN));
    m_maximizeHost->layout().setPosition(Edge::Top, Length::points(MAXIMIZE_MARGIN));
    m_maximizeHost->layout().setPosition(Edge::Right, Length::points(MAXIMIZE_MARGIN));
    m_maximizeHost->layout().setPosition(Edge::Bottom, Length::points(MAXIMIZE_MARGIN));
    m_maximizeRoot->addSubview(m_maximizeHost);

    m_strip = std::make_shared<VisibilityStripView>(this);

    m_root = std::make_shared<Panel>();
    m_root->setBackgroundColor(ThemeColor::windowBackground); // editor window root → carries the base background
    m_root->layout().setFlexDirection(FlexDirection::Column);
    m_root->layout().setFlexGrow(1.f);
    m_root->addSubview(std::move(toolbar));
    m_root->addSubview(m_workspace);
    m_root->addSubview(m_strip);
    m_root->addSubview(m_maximizeRoot);

    rebuild();
}

/* ------------------------------ helpers ------------------------------ */

std::shared_ptr<TabView> DockManager::dockTabView(Dock dock) const {
    switch (dock) {
        case Dock::Left: return m_leftDock;
        case Dock::Right: return m_rightDock;
        case Dock::Bottom: return m_bottomDock;
        case Dock::Center: return m_center; // the center is tabbed too: scenes + added panels
    }
    return nullptr;
}

std::shared_ptr<View> DockManager::dockZone(Dock dock) const {
    switch (dock) {
        case Dock::Left: return m_leftDock;
        case Dock::Right: return m_rightDock;
        case Dock::Bottom: return m_bottomDock;
        case Dock::Center: return m_center;
    }
    return nullptr;
}

std::shared_ptr<IconButton> DockManager::maxButton(Dock dock) const {
    switch (dock) {
        case Dock::Left: return m_leftMax;
        case Dock::Right: return m_rightMax;
        case Dock::Bottom: return m_bottomMax;
        case Dock::Center: return m_centerMax;
    }
    return nullptr;
}

DockPanel *DockManager::findPanel(const std::string &id) {
    for (DockPanel &p : m_panels) {
        if (p.id == id) { return &p; }
    }
    return nullptr;
}

const DockPanel *DockManager::findPanel(const std::string &id) const {
    for (const DockPanel &p : m_panels) {
        if (p.id == id) { return &p; }
    }
    return nullptr;
}

bool DockManager::hasPanels(Dock dock) const {
    for (const DockPanel &p : m_panels) {
        if (p.open && p.dock == dock) { return true; }
    }
    return false;
}

bool DockManager::userVisible(Dock dock) const {
    switch (dock) {
        case Dock::Left: return m_leftUserVisible;
        case Dock::Right: return m_rightUserVisible;
        case Dock::Bottom: return m_bottomUserVisible;
        case Dock::Center: return true;
    }
    return true;
}

int DockManager::tabIndexOf(const std::string &id) const {
    const DockPanel *target = findPanel(id);
    if (!target || !target->open) { return -1; }
    int idx = 0;
    for (const DockPanel &p : m_panels) {
        if (p.id == id) { return idx; }
        if (p.open && p.dock == target->dock) { ++idx; }
    }
    return -1;
}


void DockManager::setStripStatusText(const std::string &text) {
    if (m_strip) { m_strip->setStatusText(text); }
}
void DockManager::setPanelDirty(const std::string &id, bool dirty) {
    const DockPanel *panel = findPanel(id);
    if (!panel) { return; }
    std::shared_ptr<TabView> tabs = dockTabView(panel->dock);
    const int index = tabIndexOf(id);
    if (tabs && index >= 0) { tabs->setTabDirty(index, dirty); }
}

/* ------------------------------ transient panels (editor tabs) ------------------------------ */

bool DockManager::activateTransientPanel(const std::string &id) {
    DockPanel *existing = findPanel(id);
    if (!existing) { return false; }
    // Activation in the tab's CURRENT dock (the user's drag is respected), without changing the focus.
    const auto tv = dockTabView(existing->dock);
    setVisible(existing->dock, true);
    if (tv) { tv->setSelectedIndex(tabIndexOf(existing->id)); }
    refreshStrip();
    return true;
}

void DockManager::openTransientPanel(DockPanel panel, Dock dock) {
    if (activateTransientPanel(panel.id)) { return; }
    panel.transient = true;
    panel.open = true;
    panel.inStrip = false; // temporary tabs do not take part in the visibility strip
    panel.dock = dock;
    m_panelTabId[panel.id] = m_nextTabId++;
    m_panels.push_back(std::move(panel));
    const std::string id = m_panels.back().id;
    rebuild();
    setVisible(dock, true);
    // Selection without moving the focus: the user selects objects in a row from the Hierarchy/viewport.
    if (const auto tv = dockTabView(dock)) { tv->setSelectedIndex(tabIndexOf(id)); }
    refreshStrip();
}

void DockManager::closeTransientPanel(const std::string &id) {
    const DockPanel *p = findPanel(id);
    if (!p || !p->transient) { return; }
    closePanelInternal(id);
    rebuild();
}

void DockManager::renameTransientPanel(const std::string &oldId, const std::string &newId) {
    DockPanel *p = findPanel(oldId);
    if (!p || !p->transient || oldId == newId || findPanel(newId)) { return; }
    const auto it = m_panelTabId.find(oldId);
    if (it == m_panelTabId.end()) { return; }
    const std::uint64_t tid = it->second; // same tab-id — the tab/position/selection do not change
    m_panelTabId.erase(it);               // erase before insert: oldId may refer to p->id
    p->id = newId;
    m_panelTabId[p->id] = tid;
}

void DockManager::updateTransientPanel(
    const std::string &id, std::string title, const std::string &icon, std::shared_ptr<View> content
) {
    DockPanel *p = findPanel(id);
    if (!p || !p->transient) { return; }
    const auto tv = dockTabView(p->dock);
    const int index = tabIndexOf(id);
    const bool sameContent = p->content == content; // replace preview reuses the tab's view
    p->title = title;
    p->icon = icon;
    p->content = content;
    if (!tv || index < 0) { return; } // dock hidden by the user — the tab will update from the specification
    tv->setTabTitle(index, std::move(title));
    tv->setTabIcon(index, icons().get(icon));
    if (!sameContent) { tv->replaceTabContent(index, std::move(content)); }
}

void DockManager::setPanelPreview(const std::string &id, bool preview) {
    DockPanel *p = findPanel(id);
    if (!p) { return; }
    p->preview = preview;
    const auto tv = dockTabView(p->dock);
    const int index = tabIndexOf(id);
    if (tv && index >= 0) { tv->setTabPreview(index, preview); }
}

bool DockManager::panelExists(const std::string &id) const {
    return findPanel(id) != nullptr;
}

void DockManager::setOnTransientClosed(std::function<void(const std::string &)> callback) {
    m_onTransientClosed = std::move(callback);
}

void DockManager::setOnPanelPinned(std::function<void(const std::string &)> callback) {
    m_onPanelPinned = std::move(callback);
}

void DockManager::setOnTransientCloseRequest(std::function<bool(const std::string &)> callback) {
    m_onTransientCloseRequest = std::move(callback);
}

void DockManager::setAddMenuItemsProvider(std::function<std::vector<AddMenuItem>()> provider) {
    m_addMenuItems = std::move(provider);
}

bool DockManager::allowClose(const std::string &id) {
    const DockPanel *p = findPanel(id);
    if (!p || !p->transient) { return true; } // regular panels are hidden freely
    return !m_onTransientCloseRequest || m_onTransientCloseRequest(id);
}

void DockManager::closePanelInternal(const std::string &id) {
    DockPanel *p = findPanel(id);
    if (!p) { return; }
    if (!p->transient) {
        p->open = false; // a regular panel: leaves the dock, available through the strip and "+"
        return;
    }
    m_panelTabId.erase(id);
    const std::string closedId = id; // a copy: the erase below destroys the owning string
    m_panels.erase(
        std::remove_if(m_panels.begin(), m_panels.end(), [&](const DockPanel &q) { return q.id == closedId; }),
        m_panels.end()
    );
    if (m_onTransientClosed) { m_onTransientClosed(closedId); }
}

void DockManager::tabDoubleClicked(Dock dock, int tabIndex) {
    const auto tv = dockTabView(dock);
    if (!tv) { return; }
    const std::string id = panelForTabId(tv->tabId(tabIndex));
    DockPanel *p = findPanel(id);
    if (!p || !p->preview) { return; } // pinning concerns only preview tabs
    p->preview = false;
    tv->setTabPreview(tabIndex, false);
    if (m_onPanelPinned) { m_onPanelPinned(id); }
}

std::string DockManager::selectedPanelId(Dock dock) const {
    // Through the stable tab-id rather than by recounting over the specification: the specification
    // may have already mutated (reopen/move) — counting over it would return the neighboring panel.
    const auto tv = dockTabView(dock);
    if (!tv || tv->selectedIndex() < 0) { return {}; }
    return panelForTabId(tv->tabId(tv->selectedIndex()));
}

void DockManager::showTabMenu(Dock dock, int tabIndex, Point position) {
    const auto tv = dockTabView(dock);
    if (!tv) { return; }
    const std::string id = panelForTabId(tv->tabId(tabIndex)); // a copy: the menu outlives the rebuilds
    DockPanel *panel = findPanel(id);
    if (!panel) { return; }
    Context *context = tv->getContext();
    if (!context) { return; }

    auto menu = std::make_shared<Menu>();
    if (panel->closable) {
        menu->addItem("Close", [this, id] {
            if (!allowClose(id)) { return; } // a dirty transient may cancel the close
            closePanelInternal(id);          // a transient is removed, a regular one — open=false
            rebuild();
        });
    }
    bool hasOthers = false;
    for (const DockPanel &p : m_panels) {
        if (p.open && p.dock == dock && p.closable && p.id != id) {
            hasOthers = true;
            break;
        }
    }
    if (hasOthers) {
        menu->addItem("Close Others", [this, dock, id] {
            // Collect the ids first: closePanelInternal mutates m_panels (a transient is removed).
            std::vector<std::string> toClose;
            for (const DockPanel &p : m_panels) {
                if (p.open && p.dock == dock && p.closable && p.id != id) { toClose.push_back(p.id); }
            }
            for (const std::string &closeId : toClose) {
                if (allowClose(closeId)) { closePanelInternal(closeId); } // a refusal skips that one
            }
            rebuild();
        });
    }
    if (panel->closable) { // moving — only non-fixed ones (the viewport stays in the center)
        menu->addSeparator();
        const std::pair<const char *, Dock> targets[] = {
            {"Dock Left", Dock::Left},
            {"Dock Right", Dock::Right},
            {"Dock Bottom", Dock::Bottom},
            {"Dock Center", Dock::Center},
        };
        for (const auto &[title, target] : targets) {
            if (target == panel->dock) { continue; }
            menu->addItem(title, [this, id, target] { stripDidRequestDock(id, target); });
        }
    }
    if (panel->closable || hasOthers) { menu->addSeparator(); }
    menu->addItem("Maximize", [this, dock] { maximize(dock); });

    auto popup = std::make_shared<Popup>(menu, Rect(position.x, position.y, 0.f, 0.f));
    menu->setOnItemSelected([popup = popup.get()] { popup->dismiss(); });
    context->addOverlay(popup);
}

Panda::DockLayoutState DockManager::captureLayout() const {
    Panda::DockLayoutState state;
    state.panels.reserve(m_panels.size());
    for (const DockPanel &p : m_panels) {
        if (p.transient) {
            // Pinned editor tabs are saved as a separate list (the id encodes the object),
            // the preview one is session-only and goes away.
            if (!p.preview) {
                Panda::PinnedTabPlacement pinned;
                pinned.id = p.id;
                pinned.dock = static_cast<int>(p.dock);
                state.pinnedTabs.push_back(std::move(pinned));
            }
            continue;
        }
        Panda::DockPanelPlacement placement;
        placement.id = p.id;
        placement.dock = static_cast<int>(p.dock);
        placement.open = p.open;
        state.panels.push_back(std::move(placement));
    }
    state.leftVisible = m_leftUserVisible;
    state.rightVisible = m_rightUserVisible;
    state.bottomVisible = m_bottomUserVisible;
    state.leftWeight = m_workspace->paneWeight(0);
    state.centerColumnWeight = m_workspace->paneWeight(1);
    state.rightWeight = m_workspace->paneWeight(2);
    state.centerWeight = m_centerColumn->paneWeight(0);
    state.bottomWeight = m_centerColumn->paneWeight(1);
    state.selectedLeft = selectedPanelId(Dock::Left);
    state.selectedRight = selectedPanelId(Dock::Right);
    state.selectedBottom = selectedPanelId(Dock::Bottom);
    return state;
}

void DockManager::applyLayout(const Panda::DockLayoutState &state) {
    if (!state.isValid()) { return; }

    // Reorder the specification by the saved placements: unknown ids from the settings are ignored,
    // new panels (which did not exist at save time) stay at the end with their defaults.
    std::vector<DockPanel> ordered;
    ordered.reserve(m_panels.size());
    for (Panda::DockPanelPlacement placement : state.panels) {
        if (placement.id == "console") { placement.id = "output"; } // migration: the panel was renamed
        auto it = std::find_if(m_panels.begin(), m_panels.end(), [&](const DockPanel &p) {
            return p.id == placement.id;
        });
        if (it == m_panels.end()) { continue; }
        DockPanel panel = std::move(*it);
        m_panels.erase(it);
        if (panel.closable) { // fixed ones (the viewport) do not move — their zone is fixed
            panel.dock = static_cast<Dock>(std::clamp(placement.dock, 0, 3));
            panel.open = placement.open;
        }
        ordered.push_back(std::move(panel));
    }
    for (DockPanel &p : m_panels) {
        ordered.push_back(std::move(p));
    }
    m_panels = std::move(ordered);

    m_leftUserVisible = state.leftVisible;
    m_rightUserVisible = state.rightVisible;
    m_bottomUserVisible = state.bottomVisible;
    rebuild();

    if (state.leftWeight > 0.f) { m_workspace->setPaneWeight(0, state.leftWeight); }
    if (state.centerColumnWeight > 0.f) { m_workspace->setPaneWeight(1, state.centerColumnWeight); }
    if (state.rightWeight > 0.f) { m_workspace->setPaneWeight(2, state.rightWeight); }
    if (state.centerWeight > 0.f) { m_centerColumn->setPaneWeight(0, state.centerWeight); }
    if (state.bottomWeight > 0.f) { m_centerColumn->setPaneWeight(1, state.bottomWeight); }

    const Dock docks[] = {Dock::Left, Dock::Right, Dock::Bottom};
    const std::string *selectedIds[] = {&state.selectedLeft, &state.selectedRight, &state.selectedBottom};
    for (int i = 0; i < 3; ++i) {
        if (selectedIds[i]->empty()) { continue; }
        const int idx = tabIndexOf(*selectedIds[i]);
        if (idx >= 0) { dockTabView(docks[i])->setSelectedIndex(idx); }
    }
    refreshStrip();
}

std::shared_ptr<View> DockManager::makeDockTrailing(Dock dock) {
    auto row = std::make_shared<Panel>();
    row->setBackgroundColor(Color(0x00000000));
    row->layout().setFlexDirection(FlexDirection::Row);
    row->layout().setAlignItems(Align::Center);
    row->layout().setGap(2.f);

    // "+" on every dock, including the center (bottom panels can be added there next to the scenes).
    auto add = std::make_shared<IconButton>(Ic::Plus, Ic::HeaderSize, 23.f);
    add->setTooltip("Add Panel");
    IconButton *addPtr = add.get();
    add->setOnClick([this, dock, addPtr] { showAddMenu(dock, addPtr); });
    row->addSubview(add);

    auto full = std::make_shared<IconButton>(Ic::Maximize, Ic::HeaderSize, 23.f);
    full->setTooltip("Toggle Fullscreen");
    full->setOnClick([this, dock] { maximize(dock); });
    row->addSubview(full);
    switch (dock) {
        case Dock::Left: m_leftMax = full; break;
        case Dock::Right: m_rightMax = full; break;
        case Dock::Bottom: m_bottomMax = full; break;
        case Dock::Center: m_centerMax = full; break;
    }
    return row;
}

/* ------------------------------ layout ------------------------------ */

void DockManager::rebuild() {
    m_inRebuild = true; // setSelectedIndex below must not trigger refreshStrip for every dock
    // Remember the active tab of each dock so that changing one does not reset the selection in others.
    const Dock docks[] = {Dock::Left, Dock::Right, Dock::Bottom, Dock::Center};
    std::string selected[] = {selectedPanelId(Dock::Left), selectedPanelId(Dock::Right),
                              selectedPanelId(Dock::Bottom), selectedPanelId(Dock::Center)};

    m_leftDock->removeAllTabs();
    m_rightDock->removeAllTabs();
    m_bottomDock->removeAllTabs();
    m_center->removeAllTabs(); // the center is re-laid out too (scenes + center panels from the specification)
    for (const DockPanel &p : m_panels) {
        if (!p.open) { continue; }
        // draggable = closable: fixed panels (viewport, closable=false) cannot be dragged out of the dock.
        if (auto dock = dockTabView(p.dock)) {
            const int idx =
                dock->addTab(p.title, p.content, p.closable, tabIdForPanel(p.id), p.closable, icons().get(p.icon));
            if (p.preview) { dock->setTabPreview(idx, true); } // preview lives in the specification, style in the tab
        }
    }

    for (int i = 0; i < 4; ++i) {
        if (selected[i].empty()) { continue; }
        const int idx = tabIndexOf(selected[i]);
        if (idx >= 0) { dockTabView(docks[i])->setSelectedIndex(idx); }
    }

    applyDockVisibility(Dock::Left);
    applyDockVisibility(Dock::Right);
    applyDockVisibility(Dock::Bottom);
    m_inRebuild = false;
    refreshStrip();
}

void DockManager::applyDockVisibility(Dock dock) {
    const bool vis = isVisible(dock);
    switch (dock) {
        case Dock::Left: m_workspace->setPaneVisible(0, vis); break;
        case Dock::Right: m_workspace->setPaneVisible(2, vis); break;
        case Dock::Bottom: m_centerColumn->setPaneVisible(1, vis); break;
        case Dock::Center: break;
    }
}

bool DockManager::isVisible(Dock dock) const {
    if (dock == Dock::Center) { return true; }
    return userVisible(dock) && hasPanels(dock); // an empty dock is hidden
}

void DockManager::setVisible(Dock dock, bool visible) {
    switch (dock) {
        case Dock::Left: m_leftUserVisible = visible; break;
        case Dock::Right: m_rightUserVisible = visible; break;
        case Dock::Bottom: m_bottomUserVisible = visible; break;
        case Dock::Center: return;
    }
    applyDockVisibility(dock);
}

void DockManager::refreshStrip() {
    std::vector<StripItem> items;
    items.reserve(m_panels.size());
    for (const DockPanel &p : m_panels) {
        if (!p.inStrip) { continue; } // center scenes are not in the strip; closed panels are shown (dim)
        const auto tv = dockTabView(p.dock);
        const bool active = p.open && isVisible(p.dock) && tv && tv->selectedIndex() == tabIndexOf(p.id);
        items.push_back(StripItem{p.id, p.icon, p.dock, p.open, active});
    }
    m_strip->setItems(items);
}

/* ------------------------------ strip / close / add commands ------------------------------ */

void DockManager::showPanel(const std::string &idRef) {
    const std::string id = idRef; // a copy at the boundary (see stripDidActivatePanel)
    DockPanel *p = findPanel(id);
    if (!p) { return; }
    if (!p->open) {
        reopenPanel(id, p->dock); // a closed tab → appears in its standard dock
        return;
    }
    const Dock dock = p->dock;
    const auto tv = dockTabView(dock);
    if (!tv) { return; }
    setVisible(dock, true);
    tv->setSelectedIndex(tabIndexOf(id));
    tv->focusSelectedContent(); // showing = also focusing the selected tab
    refreshStrip();
}

void DockManager::stripDidActivatePanel(const std::string &idRef) {
    // A copy at the boundary: idRef lives in the lambda of a strip button, and rebuild/refreshStrip below
    // recreate the buttons and destroy that lambda together with the string.
    const std::string id = idRef;
    DockPanel *p = findPanel(id);
    if (!p) { return; }
    if (p->open) {
        const Dock dock = p->dock;
        const auto tv = dockTabView(dock);
        const bool active = tv && isVisible(dock) && tv->selectedIndex() == tabIndexOf(id);
        if (active) {
            setVisible(dock, false); // active and visible → hide the dock (as in Zed)
            refreshStrip();
            return;
        }
    }
    showPanel(id);
}

void DockManager::stripDidRequestDock(const std::string &idRef, Dock dock) {
    const std::string id = idRef; // a copy at the boundary (see stripDidActivatePanel)
    DockPanel *p = findPanel(id);
    if (!p || p->dock == dock) { return; } // Center is allowed now — it is a full-fledged dock
    p->dock = dock;
    rebuild();
    setVisible(dock, true);
    if (auto tv = dockTabView(dock)) {
        tv->setSelectedIndex(tabIndexOf(id));
        tv->focusSelectedContent();
    }
    refreshStrip();
}

/* ------------------------------ tab drag-and-drop (group «docks») ------------------------------ */

std::uint64_t DockManager::tabIdForPanel(const std::string &id) const {
    auto it = m_panelTabId.find(id);
    return it != m_panelTabId.end() ? it->second : 0;
}

std::string DockManager::panelForTabId(std::uint64_t tabId) const {
    for (const auto &[id, tid] : m_panelTabId) {
        if (tid == tabId) { return id; }
    }
    return {};
}

void DockManager::movePanelTo(const std::string &id, Dock dock, int destIndex) {
    DockPanel *p = findPanel(id);
    if (!p) { return; }
    DockPanel moved = *p; // copy (shared_ptr content); repositioned in the specification keeping the others' order
    moved.dock = dock;
    moved.open = true;
    m_panels.erase(
        std::remove_if(m_panels.begin(), m_panels.end(), [&](const DockPanel &q) { return q.id == id; }),
        m_panels.end()
    );
    // Insert before the destIndex-th open panel of the target dock (otherwise — at the end).
    int seen = 0;
    std::size_t insertPos = m_panels.size();
    for (std::size_t i = 0; i < m_panels.size(); ++i) {
        if (!m_panels[i].open || m_panels[i].dock != dock) { continue; }
        if (seen == destIndex) {
            insertPos = i;
            break;
        }
        ++seen;
    }
    m_panels.insert(m_panels.begin() + static_cast<std::ptrdiff_t>(insertPos), moved);
}

void DockManager::tabReordered(Dock dock, int toIndex) {
    const auto tv = dockTabView(dock);
    if (!tv) { return; }
    const std::string id = panelForTabId(tv->tabId(toIndex));
    if (id.empty()) { return; }
    // TabView has already rearranged the tabs visually — only sync the specification (no rebuild).
    movePanelTo(id, dock, toIndex);
    refreshStrip();
}

void DockManager::tabMovedToDock(Dock dock, std::uint64_t tabId, int destIndex) {
    const std::string id = panelForTabId(tabId);
    DockPanel *p = findPanel(id);
    if (!p || p->dock == dock) { return; }
    movePanelTo(id, dock, destIndex);
    rebuild(); // the tab is still in the source bar — the rebuild moves it into the target one
    setVisible(dock, true);
    if (const auto tv = dockTabView(dock)) { tv->setSelectedIndex(tabIndexOf(id)); }
    refreshStrip();
}

void DockManager::panelClosed(Dock dock, int tabIndex) {
    int idx = 0;
    for (DockPanel &p : m_panels) {
        if (!p.open || p.dock != dock) { continue; }
        if (idx == tabIndex) {
            closePanelInternal(p.id); // regular → open=false (available through "+"); transient — removed
            rebuild();
            return;
        }
        ++idx;
    }
}

void DockManager::reopenPanel(std::string id, Dock dock) { // id by value — see the header
    if (!findPanel(id)) { return; }
    // A tab being opened is added AT THE END of the bar (as in browsers) rather than at its old position
    // in the specification — otherwise it wedges in between the existing tabs.
    movePanelTo(id, dock, static_cast<int>(m_panels.size()));
    rebuild();
    setVisible(dock, true);
    if (auto tv = dockTabView(dock)) {
        tv->setSelectedIndex(tabIndexOf(id));
        tv->focusSelectedContent();
    }
    refreshStrip();
}

void DockManager::showAddMenu(Dock dock, View *anchor) {
    auto menu = std::make_shared<Menu>();
    bool any = false;
    for (const DockPanel &p : m_panels) {
        // Only closed ones; the non-closable center scenes are not added. Panels outside the strip
        // (Statistics/Profiler) are added exactly from here. There are no closed transients
        // (closing removes them) — a guard for the future.
        if (p.open || !p.closable || p.transient) { continue; }
        const std::string id = p.id;
        menu->addItem(p.title, [this, id, dock] { reopenPanel(id, dock); }); // open the closed one in this dock
        any = true;
    }
    // The documents (the settings of the project and of the editor, the documents of modules):
    // always listed — an open one is activated by its own opening path.
    const std::vector<AddMenuItem> documents = m_addMenuItems ? m_addMenuItems() : std::vector<AddMenuItem>{};
    if (!documents.empty() && any) { menu->addSeparator(); }
    for (const AddMenuItem &item : documents) {
        menu->addItem(item.title, [open = item.open, dock] {
            if (open) { open(dock); }
        });
        any = true;
    }
    if (!any) { return; } // nothing to add
    const Rect f = anchor->getWorldFrame();
    auto popup = std::make_shared<Popup>(menu, f);
    menu->setOnItemSelected([p = popup.get()] { p->dismiss(); });
    if (Context *ctx = m_root->getContext()) { ctx->addOverlay(popup); }
}

/* ------------------------------ maximize ------------------------------ */

bool DockManager::dockContaining(View *view, Dock &out) const {
    for (View *v = view; v; v = v->getSuperview()) {
        if (v == m_leftDock.get()) { out = Dock::Left; return true; }
        if (v == m_rightDock.get()) { out = Dock::Right; return true; }
        if (v == m_bottomDock.get()) { out = Dock::Bottom; return true; }
        if (v == m_center.get()) { out = Dock::Center; return true; }
    }
    return false;
}

void DockManager::maximize(Dock dock) {
    if (m_zoomView) {
        const bool sameZone = m_maximizedDock == dock;
        restoreMaximized();
        if (sameZone) { return; }
    }
    std::shared_ptr<View> zone = dockZone(dock);
    if (!zone) { return; }
    Context *ctx = m_root->getContext();
    View *keepFocus = ctx ? ctx->getFocusedView() : nullptr; // keep focus across the zone re-parent
    m_zoomView = zone;
    m_zoomHome = zone->getSuperview();
    m_maximizeHost->addSubview(zone);
    m_maximizeRoot->layout().setDisplay(Display::Flex);
    m_maximizedDock = dock;
    if (auto btn = maxButton(dock)) { btn->setIconName(Ic::Restore); } // maximized → the "restore" icon
    if (ctx && keepFocus) { ctx->setFocusedView(keepFocus); }          // the focus stays where it was
    m_root->setNeedsLayout();
}

void DockManager::maximizeContaining(View *view) {
    Dock dock;
    if (dockContaining(view, dock)) { maximize(dock); }
}

void DockManager::restoreMaximized() {
    if (!m_zoomView) { return; }
    Context *ctx = m_root->getContext();
    View *keepFocus = ctx ? ctx->getFocusedView() : nullptr; // keep focus across the re-parent back
    if (auto btn = maxButton(m_maximizedDock)) { btn->setIconName(Ic::Maximize); } // restored → "maximize"
    if (m_zoomHome) { m_zoomHome->addSubview(m_zoomView); }
    m_maximizeRoot->layout().setDisplay(Display::None);
    m_zoomView = nullptr;
    m_zoomHome = nullptr;
    if (ctx && keepFocus) { ctx->setFocusedView(keepFocus); }
    m_root->setNeedsLayout();
}

} // namespace Editor
