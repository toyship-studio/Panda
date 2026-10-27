#include "PandaUI/Elements/TabView.hpp"

#include "PandaUI/Core/Theme.hpp"
#include "PandaUI/Elements/Control.hpp"
#include "PandaUI/Elements/ImageView.hpp"
#include "PandaUI/Elements/Label.hpp"
#include "PandaUI/Elements/ScrollView.hpp"
#include "PandaUI/Rendering/UIRenderer.hpp"
#include "PandaUI/Runtime/Context.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace PandaUI {

namespace {
    constexpr const char *TAB_DRAG_TYPE = "tabview.tab"; // payload.type for dragging tabs

    // The first focusable view in the subtree (focus goes to the main view of the tab on click).
    View *firstFocusable(View *view) {
        if (!view || view->isHidden()) { return nullptr; }
        if (view->canBecomeFocused()) { return view; }
        for (const std::shared_ptr<View> &sub : view->getSubviews()) {
            if (View *found = firstFocusable(sub.get())) { return found; }
        }
        return nullptr;
    }
} // namespace

// Tab header. No hover background: the only reaction to hovering is the close cross (it is drawn,
// not a child control → it does not take the hover away from the tab, hence stable). It is dragged via
// the shared drag-and-drop (prepareDrag); a plain click in the cross zone closes, otherwise it selects.
class TabView::Tab final : public Control {
public:
    Tab(TabView *owner, std::string title, bool closable, std::uint64_t id, bool draggable, TextureHandle icon)
        : m_owner(owner)
        , m_closable(closable)
        , m_draggable(draggable)
        , m_id(id)
        , m_title(title)
        , m_label(std::make_shared<Label>(std::move(title))) {
        setCursor(Cursor::PointingHand);
        setFocusable(false);
        layout().setFlexDirection(FlexDirection::Row);
        layout().setAlignItems(Align::Center);
        layout().setFlexShrink(0.f);
        layout().setPadding(Edge::Right, 0.f); // every tab has the trailing slot (dot/cross)

        if (icon.isValid()) {
            m_icon = std::make_shared<ImageView>();
            m_icon->setTexture(icon);
            m_icon->setContentMode(ImageContentMode::Fit);
            m_icon->setUserInteractionEnabled(false);
            m_icon->layout().setFlexShrink(0.f);
            addSubview(m_icon);
        }

        m_label->layout().setFlexShrink(1.f);
        addSubview(m_label);

        // Trailing slot (not interactive → does not steal the hover). It holds the "unsaved" dot —
        // on pinned tabs as well (the viewport); on closable ones the cross is drawn over the dot.
        m_slot = std::make_shared<Panel>();
        m_slot->setBackgroundColor(Color(0x00000000));
        m_slot->setUserInteractionEnabled(false);
        m_slot->layout().setFlexShrink(0.f);
        m_slot->layout().setJustifyContent(Justify::Center);
        m_slot->layout().setAlignItems(Align::Center);

        m_dot = std::make_shared<Panel>();
        m_dot->setUserInteractionEnabled(false);
        m_slot->addSubview(m_dot);
        addSubview(m_slot);

        applyAppearance();
    }

    // The geometry and the font of the appearance; then the state colors.
    void applyAppearance() {
        const TabViewAppearance &a = m_owner->appearance();
        layout().setHeight(Length::points(a.tabHeight));
        layout().setPadding(Edge::Left, a.tabPaddingLeft);
        surface().setCornerRadius(a.tabRadius);
        if (m_icon) {
            m_icon->layout().setSize({a.iconSize, a.iconSize});
            m_icon->layout().setMargin(Edge::Right, a.iconGap);
        }
        m_label->setFont(a.font.withItalic(m_preview));
        m_slot->layout().setWidth(Length::points(a.closeSlot));
        m_dot->layout().setSize({a.dotSize, a.dotSize});
        m_dot->surface().setCornerRadius(a.dotSize * 0.5f);
        applyStyle();
    }

    void setTitle(std::string title) {
        m_title = title;
        m_label->setText(std::move(title));
    }
    void setSelected(bool selected) {
        if (m_selected == selected) { return; }
        m_selected = selected;
        applyStyle();
    }
    void setDirty(bool dirty) {
        if (m_dirty == dirty) { return; }
        m_dirty = dirty;
        applyStyle();
    }
    void setPreview(bool preview) {
        if (m_preview == preview) { return; }
        m_preview = preview;
        m_label->setFont(m_label->getFont().withItalic(preview)); // unpinned — in italics
        applyStyle();
    }
    bool preview() const {
        return m_preview;
    }
    void setIcon(TextureHandle icon) {
        if (!icon.isValid()) {
            if (m_icon) {
                removeSubview(m_icon);
                m_icon = nullptr;
            }
            return;
        }
        if (!m_icon) {
            const TabViewAppearance &a = m_owner->appearance();
            m_icon = std::make_shared<ImageView>();
            m_icon->setContentMode(ImageContentMode::Fit);
            m_icon->setUserInteractionEnabled(false);
            m_icon->layout().setSize({a.iconSize, a.iconSize});
            m_icon->layout().setFlexShrink(0.f);
            m_icon->layout().setMargin(Edge::Right, a.iconGap);
            // There is no insertSubview — the icon must go BEFORE the title: rebuild the order of the children.
            removeSubview(m_label);
            removeSubview(m_slot);
            addSubview(m_icon);
            addSubview(m_label);
            addSubview(m_slot);
        }
        m_icon->setTexture(icon);
        applyStyle(); // tint according to the current selected state
    }

protected:
    bool pointerDown(PointerEvent &event) override {
        if (event.button == PointerButton::Right) {
            m_owner->tabContextMenu(this, event.position); // right-click → tab context menu
            return true;
        }
        // A double-click is detected ONLY on down (Context fills clickCount for down events; on release
        // it is always 1). The first click of the pair has already selected the tab — the second one pins it.
        const float localX = event.position.x - getWorldFrame().origin.x;
        const bool inCloseZone = showClose() && localX >= getFrame().size.width - m_owner->appearance().closeSlot;
        if (event.button == PointerButton::Left && event.clickCount >= 2 && !inCloseZone) {
            m_owner->tabDoubleClicked(this);
        }
        return true; // capture: a move >threshold becomes a drag, a plain release — select/close
    }
    bool pointerUp(PointerEvent &event) override {
        // Only a plain click gets here: during a drag Context sends interactionCancelled, and the release
        // goes to finishDrag. A right click is handled on down (menu) — the release does not select.
        if (event.button != PointerButton::Left) { return true; }
        const float localX = event.position.x - getWorldFrame().origin.x;
        if (showClose() && localX >= getFrame().size.width - m_owner->appearance().closeSlot) {
            m_owner->closeTab(this);
        } else {
            m_owner->selectTab(this);
        }
        return true;
    }
    bool prepareDrag(DragPayload &payload, Point) override {
        if (!m_draggable) { return false; } // a pinned tab (the viewport) cannot be dragged
        payload.type = TAB_DRAG_TYPE;
        payload.source = m_owner; // the owner bar: identity (reorder/transfer) + group check
        payload.items = {m_id};
        payload.ghostText = m_title;
        return true;
    }
    void hoverChanged(bool hovered) override {
        m_hovered = hovered;
        applyStyle();
    }
    void themeChanged() override {
        applyStyle();
    }
    void draw(float offsetX, float offsetY) override {
        Context *context = getContext();
        if (!context) { return; }
        UIRenderer &renderer = context->getRenderer();
        const TabViewAppearance &a = m_owner->appearance();
        const Rect f = getFrame();

        // A thin vertical separator on the left (between tabs), except for the very first tab — full height.
        if (m_owner->indexOfTab(this) > 0 && a.separatorWidth > 0.f) {
            const float x = offsetX + f.origin.x + a.separatorWidth * 0.5f;
            renderer.drawLine(
                Point(x, offsetY + f.origin.y), Point(x, offsetY + f.origin.y + f.size.height), a.separatorWidth,
                a.separator.resolve()
            );
        }

        // The cross is the only reaction to hover (on the active tab too), drawn on top.
        if (showClose()) {
            const float cx = offsetX + f.origin.x + f.size.width - a.closeSlot * 0.5f;
            const float cy = offsetY + f.origin.y + f.size.height * 0.5f;
            const float g = a.closeGlyph * 0.5f;
            const Color color = a.closeGlyphColor.resolve();
            renderer.drawLine(Point(cx - g, cy - g), Point(cx + g, cy + g), a.closeThickness, color);
            renderer.drawLine(Point(cx - g, cy + g), Point(cx + g, cy - g), a.closeThickness, color);
        }
    }

private:
    bool showClose() const {
        // Cross: always on hover; on the active tab — permanently, but it gives way to the dot
        // if there are unsaved changes (the dot is more important, the cross is available on hover).
        return m_closable && (m_hovered || (m_selected && !m_dirty));
    }
    void applyStyle() {
        const TabViewAppearance &a = m_owner->appearance();
        // Active tab = the content background → it "merges" with the area below, as in Zed. Inactive
        // ones are transparent (the strip background shows through). There is no hover plate.
        setBackgroundColor(m_selected ? a.tabSelected : a.tabNormal);
        if (m_selected && a.tabSelectedImage) {
            surface().setBackgroundImage(a.tabSelectedImage);
        } else {
            surface().clearBackgroundImage();
        }
        m_label->setTextColor(m_selected ? a.textSelected : a.textNormal);
        if (m_icon) { m_icon->setTintColor(m_selected ? a.textSelected : a.textNormal); }
        if (m_dot) {
            const bool dotShown = m_dirty && !showClose(); // the dot hides while the cross is shown
            m_dot->setHidden(!dotShown);
            m_dot->layout().setDisplay(dotShown ? Display::Flex : Display::None);
            m_dot->setBackgroundColor(m_selected ? a.textSelected : a.textNormal);
        }
        setNeedsLayout();
        setNeedsDisplay();
    }

    TabView *m_owner;
    bool m_closable;
    bool m_draggable;
    std::uint64_t m_id;
    std::string m_title;
    std::shared_ptr<Label> m_label;
    std::shared_ptr<ImageView> m_icon; // optional icon to the left of the title (tinted by selected)
    std::shared_ptr<Panel> m_slot;
    std::shared_ptr<Panel> m_dot;
    bool m_selected = false;
    bool m_dirty = false;
    bool m_preview = false;
    bool m_hovered = false;
};

TabViewAppearance &TabView::defaultAppearance() {
    static TabViewAppearance appearance;
    return appearance;
}

TabView::TabView()
    : m_appearance(defaultAppearance()) {
    setBackgroundColor(Color(0x00000000));
    layout().setFlexDirection(FlexDirection::Column);

    m_tabBar = std::make_shared<Panel>();
    m_tabBar->layout().setFlexDirection(FlexDirection::Row);
    m_tabBar->layout().setAlignItems(Align::Stretch); // tabs take the full height of the bar (no gaps)
    m_tabBar->layout().setFlexShrink(0.f);
    addSubview(m_tabBar);

    // The tabs live in a horizontal scroll: on overflow they scroll, while the trailing accessory
    // (the dock buttons) stays pinned on the right. No rubber-banding — the bar does not "stretch".
    m_tabsScroll = std::make_shared<ScrollView>();
    m_tabsScroll->setScrollDirection(ScrollDirection::Horizontal);
    m_tabsScroll->setBounces(false);
    m_tabsScroll->layout().setFlexGrow(1.f);
    m_tabsScroll->layout().setFlexShrink(1.f);
    m_tabsScroll->getContentView()->layout().setFlexDirection(FlexDirection::Row);
    m_tabsScroll->getContentView()->layout().setAlignItems(Align::Stretch);

    m_contentArea = std::make_shared<Panel>();
    m_contentArea->setBackgroundColor(Color(0x00000000));
    m_contentArea->layout().setFlexDirection(FlexDirection::Column);
    m_contentArea->layout().setFlexGrow(1.f);
    addSubview(m_contentArea);

    // Vertical indicator line of the insertion position while dragging tabs. Absolute (on top of the bar),
    // it does not take part in the flow; added last — drawn on top. Its X is set in showDropLine.
    m_dropLine = std::make_shared<Panel>();
    m_dropLine->setUserInteractionEnabled(false);
    m_dropLine->layout().setPositionType(PositionType::Absolute);
    m_dropLine->layout().setPosition(Edge::Top, Length::points(0.f));
    m_dropLine->layout().setPosition(Edge::Left, Length::points(0.f));
    m_dropLine->setHidden(true);
    m_dropLine->layout().setDisplay(Display::None);
    addSubview(m_dropLine);

    applyAppearance();
}

void TabView::setAppearance(const TabViewAppearance &appearance) {
    m_appearance = appearance;
    applyAppearance();
    for (TabItem &item : m_tabs) { item.control->applyAppearance(); }
}

void TabView::applyAppearance() {
    const TabViewAppearance &a = m_appearance;
    m_tabsScroll->layout().setHeight(Length::points(a.tabHeight));
    m_tabsScroll->getContentView()->layout().setGap(a.tabGap);
    m_dropLine->layout().setSize({a.dropLineWidth, a.tabHeight});
    m_tabBar->setBackgroundColor(a.barBackground);
    m_dropLine->setBackgroundColor(a.dropLine);
}

TabView::~TabView() = default;

int TabView::addTab(
    std::string title, std::shared_ptr<View> content, bool closable, std::uint64_t id, bool draggable,
    TextureHandle icon
) {
    if (id == 0) {
        id = m_nextTabId++;
    } else {
        m_nextTabId = std::max(m_nextTabId, id + 1); // never hand out an auto id that matches an explicit one
    }
    auto tab = std::make_shared<Tab>(this, std::move(title), closable, id, draggable, icon);

    content->layout().setFlexGrow(1.f);
    content->setHidden(true);
    content->layout().setDisplay(Display::None);
    m_contentArea->addSubview(content);

    m_tabs.push_back({tab, std::move(content), id});
    rebuildTabBar(); // headers + trailing accessory in the right order (tabs, then maximize)
    const int index = static_cast<int>(m_tabs.size()) - 1;
    if (m_selectedIndex < 0) {
        setSelectedIndex(index);
    } else {
        applySelection();
    }
    applyTabBarVisibility();
    return index;
}

std::shared_ptr<View> TabView::removeTab(int index) {
    if (index < 0 || index >= static_cast<int>(m_tabs.size())) { return nullptr; }
    TabItem item = m_tabs[index];
    m_tabBar->removeSubview(item.control);
    m_contentArea->removeSubview(item.content);
    m_tabs.erase(m_tabs.begin() + index);

    if (m_tabs.empty()) {
        m_selectedIndex = -1;
    } else if (m_selectedIndex == index) {
        m_selectedIndex = std::min(index, static_cast<int>(m_tabs.size()) - 1);
    } else if (m_selectedIndex > index) {
        --m_selectedIndex;
    }
    applySelection();
    applyContent();
    applyTabBarVisibility();
    return item.content; // return the content — the caller moves it into another TabView
}

void TabView::removeAllTabs() {
    for (const TabItem &item : m_tabs) { m_contentArea->removeSubview(item.content); }
    m_tabs.clear();
    m_selectedIndex = -1;
    rebuildTabBar(); // clears the bar (no tabs, only the trailing part remains)
    applyTabBarVisibility();
}

void TabView::setHidesTabBarWhenSingle(bool hide) {
    m_hidesTabBarWhenSingle = hide;
    applyTabBarVisibility();
}

void TabView::applyTabBarVisibility() {
    const bool hidden = m_hidesTabBarWhenSingle && m_tabs.size() <= 1;
    m_tabBar->setHidden(hidden);
    m_tabBar->layout().setDisplay(hidden ? Display::None : Display::Flex);
}

void TabView::setSelectedIndex(int index) {
    if (index < 0 || index >= static_cast<int>(m_tabs.size()) || index == m_selectedIndex) { return; }
    m_selectedIndex = index;
    applySelection();
    applyContent();
    if (m_onSelectionChanged) { m_onSelectionChanged(*this); }
}

void TabView::setTabTitle(int index, std::string title) {
    if (index >= 0 && index < static_cast<int>(m_tabs.size())) { m_tabs[index].control->setTitle(std::move(title)); }
}

void TabView::setTabDirty(int index, bool dirty) {
    if (index >= 0 && index < static_cast<int>(m_tabs.size())) { m_tabs[index].control->setDirty(dirty); }
}

void TabView::setTabPreview(int index, bool preview) {
    if (index >= 0 && index < static_cast<int>(m_tabs.size())) { m_tabs[index].control->setPreview(preview); }
}

bool TabView::tabPreview(int index) const {
    return index >= 0 && index < static_cast<int>(m_tabs.size()) && m_tabs[index].control->preview();
}

void TabView::setTabIcon(int index, TextureHandle icon) {
    if (index >= 0 && index < static_cast<int>(m_tabs.size())) { m_tabs[index].control->setIcon(icon); }
}

std::shared_ptr<View> TabView::replaceTabContent(int index, std::shared_ptr<View> content) {
    if (index < 0 || index >= static_cast<int>(m_tabs.size()) || !content) { return nullptr; }
    std::shared_ptr<View> old = m_tabs[index].content;
    m_contentArea->removeSubview(old);
    content->layout().setFlexGrow(1.f);
    m_contentArea->addSubview(content);
    m_tabs[index].content = std::move(content);
    applyContent(); // visibility of the new content according to the current selection
    return old;
}

void TabView::setOnSelectionChanged(SelectionChangedCallback callback) {
    m_onSelectionChanged = std::move(callback);
}

void TabView::setOnReorder(ReorderCallback callback) {
    m_onReorder = std::move(callback);
}

void TabView::setOnTabClosed(TabClosedCallback callback) {
    m_onClose = std::move(callback);
}

void TabView::setOnTabMoved(TabMovedCallback callback) {
    m_onTabMoved = std::move(callback);
}

void TabView::setOnTabContextMenu(TabContextMenuCallback callback) {
    m_onTabContextMenu = std::move(callback);
}

void TabView::setOnTabDoubleClicked(TabDoubleClickCallback callback) {
    m_onTabDoubleClicked = std::move(callback);
}

void TabView::setOnTabCloseRequest(TabCloseRequestCallback callback) {
    m_onTabCloseRequest = std::move(callback);
}

void TabView::tabContextMenu(Tab *tab, Point windowPosition) {
    const int index = indexOfTab(tab);
    if (index >= 0 && m_onTabContextMenu) { m_onTabContextMenu(*this, index, windowPosition); }
}

void TabView::tabDoubleClicked(Tab *tab) {
    const int index = indexOfTab(tab);
    if (index >= 0 && m_onTabDoubleClicked) { m_onTabDoubleClicked(*this, index); }
}

void TabView::setTabDragGroup(std::string group) {
    m_dragGroup = std::move(group);
}

std::uint64_t TabView::tabId(int index) const {
    return (index >= 0 && index < static_cast<int>(m_tabs.size())) ? m_tabs[index].id : 0;
}

void TabView::selectTab(Tab *tab) {
    setSelectedIndex(indexOfTab(tab));
    focusSelectedContent(); // click on a tab → focus goes to the main view of its content
}

void TabView::focusSelectedContent() {
    if (m_selectedIndex < 0) { return; }
    if (Context *context = getContext()) {
        if (View *target = firstFocusable(m_tabs[m_selectedIndex].content.get())) {
            context->setFocusedView(target);
        }
    }
}

void TabView::closeTab(Tab *tab) {
    const int idx = indexOfTab(tab);
    if (idx < 0) { return; }
    // The gate comes before any changes: the application may show a save dialog and cancel.
    if (m_onTabCloseRequest && !m_onTabCloseRequest(*this, idx)) { return; }
    const bool wasSelected = (idx == m_selectedIndex);

    TabItem item = m_tabs[idx];
    m_tabBar->removeSubview(item.control);
    m_contentArea->removeSubview(item.content);
    m_tabs.erase(m_tabs.begin() + idx);

    if (m_tabs.empty()) {
        m_selectedIndex = -1;
    } else if (m_selectedIndex == idx) {
        m_selectedIndex = std::min(idx, static_cast<int>(m_tabs.size()) - 1);
    } else if (m_selectedIndex > idx) {
        --m_selectedIndex;
    }
    applySelection();
    applyContent();

    if (m_onClose) { m_onClose(*this, idx); }
    if (wasSelected && !m_tabs.empty() && m_onSelectionChanged) { m_onSelectionChanged(*this); }
}

int TabView::indexOfTab(Tab *tab) const {
    for (int i = 0; i < static_cast<int>(m_tabs.size()); ++i) {
        if (m_tabs[i].control.get() == tab) { return i; }
    }
    return -1;
}

int TabView::indexOfTabId(std::uint64_t id) const {
    for (int i = 0; i < static_cast<int>(m_tabs.size()); ++i) {
        if (m_tabs[i].id == id) { return i; }
    }
    return -1;
}

bool TabView::canAcceptDrop(const DragInfo &drag) {
    if (drag.payload.type != TAB_DRAG_TYPE) { return false; }
    if (drag.payload.source == this) { return true; } // reorder within its own bar
    if (m_dragGroup.empty()) { return false; }         // transfer between bars — only within a shared group
    auto *src = static_cast<TabView *>(drag.payload.source);
    return src && src->m_dragGroup == m_dragGroup;
}

void TabView::dragUpdated(const DragInfo &drag) {
    showDropLine(computeInsertIndex(drag.windowPosition.x));
}

void TabView::dragExited() {
    hideDropLine();
}

bool TabView::performDrop(const DragInfo &drag) {
    const int insert = (m_dropIndex >= 0) ? m_dropIndex : computeInsertIndex(drag.windowPosition.x);
    hideDropLine();
    const std::uint64_t draggedId = drag.payload.items.empty() ? 0 : drag.payload.items.front();

    if (drag.payload.source != this) {
        // Transfer from another bar of the group: the content/panel is moved by the application (onTabMoved).
        if (!m_onTabMoved) { return false; }
        auto *src = static_cast<TabView *>(drag.payload.source);
        m_onTabMoved(*src, draggedId, insert);
        return true;
    }

    const int from = indexOfTabId(draggedId);
    if (from < 0) { return false; }
    int to = insert;
    if (to > from) { --to; } // removing from shifts the positions to the right of it
    to = std::clamp(to, 0, static_cast<int>(m_tabs.size()) - 1);
    if (to == from) { return true; }

    const std::uint64_t selId = (m_selectedIndex >= 0) ? m_tabs[m_selectedIndex].id : 0;
    TabItem item = m_tabs[from];
    m_tabs.erase(m_tabs.begin() + from);
    m_tabs.insert(m_tabs.begin() + to, item);
    if (selId) { m_selectedIndex = indexOfTabId(selId); }

    rebuildTabBar();
    applySelection();
    if (m_onReorder) { m_onReorder(*this, from, to); }
    return true;
}

int TabView::computeInsertIndex(float windowX) const {
    // World frames: the tabs lie in the scroll content, their local coordinates are shifted by the scroll.
    for (int i = 0; i < static_cast<int>(m_tabs.size()); ++i) {
        const Rect f = m_tabs[i].control->getWorldFrame();
        if (windowX < f.origin.x + f.size.width * 0.5f) { return i; }
    }
    return static_cast<int>(m_tabs.size());
}

void TabView::showDropLine(int insertIndex) {
    m_dropIndex = insertIndex;
    float x = 0.f;
    const int n = static_cast<int>(m_tabs.size());
    if (n > 0) {
        // World coordinates → TabView-local ones (the line is an absolute child of TabView).
        if (insertIndex < n) {
            x = m_tabs[insertIndex].control->getWorldFrame().origin.x;
        } else {
            const Rect f = m_tabs[n - 1].control->getWorldFrame();
            x = f.origin.x + f.size.width;
        }
        x -= getWorldFrame().origin.x;
    }
    m_dropLine->layout().setPosition(Edge::Left, Length::points(std::max(0.f, x - m_appearance.dropLineWidth * 0.5f)));
    m_dropLine->setHidden(false);
    m_dropLine->layout().setDisplay(Display::Flex);
    setNeedsLayout();
}

void TabView::hideDropLine() {
    m_dropIndex = -1;
    m_dropLine->setHidden(true);
    m_dropLine->layout().setDisplay(Display::None);
    setNeedsLayout();
}

void TabView::setTrailingAccessory(std::shared_ptr<View> accessory) {
    m_trailingAccessory = std::move(accessory);
    rebuildTabBar();
}

void TabView::rebuildTabBar() {
    m_tabsScroll->getContentView()->removeAllSubviews();
    for (const TabItem &item : m_tabs) { m_tabsScroll->getContentView()->addSubview(item.control); }
    m_tabBar->removeAllSubviews();
    m_tabBar->addSubview(m_tabsScroll); // grows and scrolls; the accessory is outside the scroll
    if (m_trailingAccessory) {
        // The wrapper centers the accessory along the bar height (the bar has alignItems Stretch) and
        // does not shrink.
        auto box = std::make_shared<Panel>();
        box->setBackgroundColor(Color(0x00000000));
        box->layout().setFlexDirection(FlexDirection::Row);
        box->layout().setAlignItems(Align::Center);
        box->layout().setFlexShrink(0.f);
        box->layout().setPadding(Edge::Right, 6.f);
        box->addSubview(m_trailingAccessory);
        m_tabBar->addSubview(box);
    }
    m_tabBar->setNeedsLayout();
}

void TabView::applySelection() {
    for (int i = 0; i < static_cast<int>(m_tabs.size()); ++i) {
        m_tabs[i].control->setSelected(i == m_selectedIndex);
    }
    // The active tab must not stay beyond the edge of the scroll. The scroll adjustment is deferred until
    // the layout is recomputed: right after addTab/rebuild the tab frames are not up to date yet.
    m_pendingRevealSelected = true;
    setNeedsLayout();
}

void TabView::viewDidCalculateLayout() {
    Panel::viewDidCalculateLayout();
    if (!m_pendingRevealSelected) { return; }
    m_pendingRevealSelected = false;
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_tabs.size())) { return; }
    const Rect tab = m_tabs[m_selectedIndex].control->getFrame(); // scroll content coordinates
    const float viewport = m_tabsScroll->getFrame().size.width;
    if (viewport <= 0.f || tab.size.width <= 0.f) { return; }
    Point offset = m_tabsScroll->getContentOffset();
    if (tab.origin.x < offset.x) {
        offset.x = tab.origin.x;
    } else if (tab.origin.x + tab.size.width > offset.x + viewport) {
        offset.x = tab.origin.x + tab.size.width - viewport;
    } else {
        return;
    }
    m_tabsScroll->setContentOffset(offset);
}

void TabView::applyContent() {
    for (int i = 0; i < static_cast<int>(m_tabs.size()); ++i) {
        const bool shown = (i == m_selectedIndex);
        m_tabs[i].content->setHidden(!shown);
        m_tabs[i].content->layout().setDisplay(shown ? Display::Flex : Display::None);
    }
    m_contentArea->setNeedsLayout();
}

void TabView::themeChanged() {
    m_tabBar->setBackgroundColor(m_appearance.barBackground); // the tokens resolve at draw
    m_dropLine->setBackgroundColor(m_appearance.dropLine);
}

} // namespace PandaUI
