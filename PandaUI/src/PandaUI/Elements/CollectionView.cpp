#include "PandaUI/Elements/CollectionView.hpp"

#include "PandaUI/Core/KeyboardEvent.hpp"
#include "PandaUI/Core/Theme.hpp"
#include "PandaUI/Elements/Control.hpp"

#include <algorithm>
#include <iterator>
#include <utility>

namespace PandaUI {

namespace {
    // Realization buffer beyond the visible area (px): smooths out fast scrolling and the one-frame
    // delay (the offset changes in the event handler, the missing cells are created in update()).
    constexpr float OVERSCAN = 220.f;
} // namespace

// Slot cell: a Control that holds the user content, draws the selection/hover and
// routes the click to CollectionView. It is reused while scrolling (recycling).
class CollectionView::Cell final : public Control {
public:
    Cell(CollectionView *owner, std::shared_ptr<View> content)
        : m_owner(owner)
        , m_content(std::move(content)) {
        setFocusable(false); // the collection itself holds the focus, not the cells
        // Selectable cells (grid/table) — pointing hand on hover, like tree rows; console log (None) — regular cursor.
        if (m_owner->m_selectionMode != CollectionSelectionMode::None) { setCursor(Cursor::PointingHand); }
        layout().setPositionType(PositionType::Absolute);
        // The content is absolutely stretched over the cell (insets 0).
        m_content->layout().setPositionType(PositionType::Absolute);
        m_content->layout().setPosition(Edge::Left, Length::points(0.f));
        m_content->layout().setPosition(Edge::Top, Length::points(0.f));
        m_content->layout().setPosition(Edge::Right, Length::points(0.f));
        m_content->layout().setPosition(Edge::Bottom, Length::points(0.f));
        addSubview(m_content);
        applyStyle();
    }

    View &content() {
        return *m_content;
    }
    int itemIndex() const {
        return m_index;
    }
    void setItemIndex(int index) {
        m_index = index;
    }

    void setCellSelected(bool selected, bool active) {
        if (m_selected == selected && m_active == active) { return; }
        m_selected = selected;
        m_active = active; // active = the collection is focused → brighter selection
        applyStyle();
    }
    void refreshStyle() { // the appearance of the owner changed
        applyStyle();
    }

    void setFrameRect(const Rect &frame) {
        layout().setPosition(Edge::Left, Length::points(frame.origin.x));
        layout().setPosition(Edge::Top, Length::points(frame.origin.y));
        layout().setWidth(Length::points(frame.size.width));
        layout().setHeight(Length::points(frame.size.height));
    }

protected:
    bool pointerDown(PointerEvent &event) override {
        if (event.button == PointerButton::Right) {
            m_owner->handleContextMenu(m_index, event.position);
            return true; // right-click — context menu, without click selection
        }
        if (event.clickCount >= 2) {
            m_owner->handleCellActivated(m_index); // double-click (selection already made by the first click)
            return true;
        }
        m_additiveClick = event.isSuperDown || event.isControlDown;
        m_rangeClick = event.isShiftDown;
        return Control::pointerDown(event);
    }
    void performPrimaryAction() override {
        m_owner->handleCellClicked(m_index, m_additiveClick, m_rangeClick);
    }
    bool prepareDrag(DragPayload &payload, Point) override {
        return m_owner->beginCellDrag(m_index, payload);
    }
    void controlStateChanged(ControlState, ControlState) override {
        applyStyle();
    }
    void themeChanged() override {
        applyStyle();
    }

private:
    void applyStyle() {
        const CollectionViewAppearance &a = m_owner->appearance();
        const bool selectable = m_owner->selectionMode() != CollectionSelectionMode::None;
        surface().setCornerRadius(a.cellRadius);
        surface().setBorderWidth(0.f);
        if (selectable && m_selected) {
            setBackgroundColor(m_active ? a.selection : a.selectionInactive); // soft plate; brighter in focus
        } else if (selectable && hasControlState(ControlState::Hovered)) {
            setBackgroundColor(a.hover);
        } else {
            setBackgroundColor(Color(0x00000000));
        }
        if (selectable && m_selected && a.selectionImage) {
            surface().setBackgroundImage(a.selectionImage);
        } else {
            surface().clearBackgroundImage();
        }
    }

    CollectionView *m_owner;
    std::shared_ptr<View> m_content;
    int m_index = -1;
    bool m_selected = false;
    bool m_active = false; // the collection is focused (for the active selection color)
    bool m_additiveClick = false;
    bool m_rangeClick = false;
};

/* ------------------------------ CollectionView ------------------------------ */

CollectionViewAppearance &CollectionView::defaultAppearance() {
    static CollectionViewAppearance appearance;
    return appearance;
}

void CollectionView::setAppearance(const CollectionViewAppearance &appearance) {
    m_appearance = appearance;
    m_recyclePool.clear();
    for (auto &entry : m_visibleCells) { entry.second->refreshStyle(); }
}

CollectionView::CollectionView(std::shared_ptr<CollectionViewLayout> layout)
    : m_appearance(defaultAppearance())
    , m_layout(std::move(layout)) {
    setScrollDirection(ScrollDirection::Vertical);
    setFocusable(true);             // the collection accepts focus → the arrow keys and the focus ring work
    setContentSize(Size(0.f, 0.f)); // disables auto-adjust: the content size is computed here from the layout
}

CollectionView::~CollectionView() = default;

void CollectionView::setDataSource(CollectionViewDataSource dataSource) {
    m_dataSource = std::move(dataSource);
    reloadData();
}

void CollectionView::setLayout(std::shared_ptr<CollectionViewLayout> layout) {
    m_layout = std::move(layout);
    reloadData();
}

void CollectionView::reconfigureItem(int index) {
    auto it = m_visibleCells.find(index);
    if (it == m_visibleCells.end() || !m_dataSource.configureCell) { return; }
    m_dataSource.configureCell(it->second->content(), index);
}

void CollectionView::reloadData() {
    m_needsReload = true;
    setNeedsDisplay();
}

void CollectionView::scrollToBottom() {
    if (!m_layout) { return; }
    const Size viewport = getFrame().size;
    const int count = m_dataSource.numberOfItems ? m_dataSource.numberOfItems() : m_itemCount;
    const float contentHeight = m_layout->contentSize(count, viewport).height;
    setContentOffset(Point(0.f, std::max(0.f, contentHeight - viewport.height)));
}

bool CollectionView::isSelected(int index) const {
    return std::find(m_selection.begin(), m_selection.end(), index) != m_selection.end();
}

void CollectionView::setSelectedIndices(std::vector<int> indices) {
    if (m_selectionMode == CollectionSelectionMode::None) {
        indices.clear();
    } else if (m_selectionMode == CollectionSelectionMode::Single && indices.size() > 1) {
        indices.resize(1);
    }
    m_selection = std::move(indices);
    m_anchorIndex = m_selection.empty() ? -1 : m_selection.front();
    m_cursorIndex = m_selection.empty() ? -1 : m_selection.back();
    applySelectionToVisible();
}

void CollectionView::clearSelection() {
    if (m_selection.empty()) { return; }
    m_selection.clear();
    m_anchorIndex = -1;
    m_cursorIndex = -1;
    applySelectionToVisible();
}

void CollectionView::setOnSelectionChanged(SelectionChangedCallback callback) {
    m_onSelectionChanged = std::move(callback);
}

void CollectionView::setOnContextMenu(ContextMenuCallback callback) {
    m_onContextMenu = std::move(callback);
}

bool CollectionView::pointerDown(PointerEvent &event) {
    // Right-click on an empty area (cells intercept their own right-click) → background context menu.
    if (event.button == PointerButton::Right) {
        handleContextMenu(-1, event.position);
        return true;
    }
    return ScrollView::pointerDown(event);
}

void CollectionView::setSelectionMode(CollectionSelectionMode mode) {
    if (m_selectionMode == mode) { return; }
    m_selectionMode = mode;
    if (mode == CollectionSelectionMode::None) {
        m_selection.clear();
        m_anchorIndex = -1;
        m_cursorIndex = -1;
    } else if (mode == CollectionSelectionMode::Single && m_selection.size() > 1) {
        m_selection.resize(1);
        m_anchorIndex = m_selection.front();
        m_cursorIndex = m_selection.front();
    }
    applySelectionToVisible();
}

bool CollectionView::viewportChanged() const {
    const Size viewport = getFrame().size;
    return viewport.width != m_lastViewport.width || viewport.height != m_lastViewport.height;
}

void CollectionView::update(double deltaTime) {
    ScrollView::update(deltaTime);
    const float offsetY = getContentOffset().y;
    if (m_needsReload || viewportChanged() || offsetY != m_lastOffsetY) {
        updateVisibleCells(m_needsReload || viewportChanged());
    }
}

void CollectionView::updateVisibleCells(bool forceRebuild) {
    if (!m_layout) { return; }
    const Size viewport = getFrame().size;
    if (viewport.height <= 0.f) { return; } // not laid out yet — retry on the next frame

    if (forceRebuild) {
        m_itemCount = m_dataSource.numberOfItems ? m_dataSource.numberOfItems() : 0;
        setContentSize(m_layout->contentSize(m_itemCount, viewport));
        // Positions/bindings are stale (reload or resize) — return all cells to the pool and build anew.
        for (auto &entry : m_visibleCells) { recycle(entry.second); }
        m_visibleCells.clear();
        m_prefetchedIndices.clear(); // the dataset changed — the previous prefetch indices no longer apply
        m_needsReload = false;
    }
    m_lastViewport = viewport;
    m_lastOffsetY = getContentOffset().y;

    if (m_itemCount <= 0 || !m_dataSource.configureCell) { return; }

    // Visible rectangle in content coordinates + a buffer above/below.
    const Rect visible(0.f, m_lastOffsetY - OVERSCAN, viewport.width, viewport.height + 2.f * OVERSCAN);
    int first = 0;
    int last = -1;
    m_layout->itemRange(visible, m_itemCount, viewport, first, last);
    first = std::max(0, first);
    last = std::min(m_itemCount - 1, last);

    // Recycle the cells that have left the range.
    for (auto it = m_visibleCells.begin(); it != m_visibleCells.end();) {
        if (it->first < first || it->first > last) {
            recycle(it->second);
            it = m_visibleCells.erase(it);
        } else {
            ++it;
        }
    }
    // Realize the missing ones within the range.
    for (int index = first; index <= last; ++index) {
        if (m_visibleCells.find(index) != m_visibleCells.end()) { continue; }
        std::shared_ptr<Cell> cell = acquireCell();
        cell->setItemIndex(index);
        m_dataSource.configureCell(cell->content(), index);
        cell->setCellSelected(isSelected(index), isFocused());
        cell->setFrameRect(m_layout->frameForItem(index, viewport));
        addContentSubview(cell);
        m_visibleCells[index] = cell;
    }
    updatePrefetch(first, last);
}

void CollectionView::updatePrefetch(int firstVisible, int lastVisible) {
    if (!m_dataSource.prefetchItems) { return; }
    // Buffer zone: a page of visible items before and after the visible range.
    const int span = std::max(8, lastVisible - firstVisible + 1);
    std::vector<int> window;
    for (int i = std::max(0, firstVisible - span); i < firstVisible; ++i) { window.push_back(i); }
    for (int i = lastVisible + 1; i <= std::min(m_itemCount - 1, lastVisible + span); ++i) { window.push_back(i); }

    std::vector<int> added, removed;
    std::set_difference(window.begin(), window.end(), m_prefetchedIndices.begin(), m_prefetchedIndices.end(),
                        std::back_inserter(added));
    std::set_difference(m_prefetchedIndices.begin(), m_prefetchedIndices.end(), window.begin(), window.end(),
                        std::back_inserter(removed));
    m_prefetchedIndices = std::move(window);
    if (!removed.empty() && m_dataSource.cancelPrefetching) { m_dataSource.cancelPrefetching(removed); }
    if (!added.empty()) { m_dataSource.prefetchItems(added); }
}

std::shared_ptr<CollectionView::Cell> CollectionView::acquireCell() {
    if (!m_recyclePool.empty()) {
        std::shared_ptr<Cell> cell = m_recyclePool.back();
        m_recyclePool.pop_back();
        return cell;
    }
    std::shared_ptr<View> content = m_dataSource.cellForReuse ? m_dataSource.cellForReuse() : std::make_shared<View>();
    return std::make_shared<Cell>(this, std::move(content));
}

void CollectionView::recycle(const std::shared_ptr<Cell> &cell) {
    getContentView()->removeSubview(cell);
    cell->setCellSelected(false, false);
    m_recyclePool.push_back(cell);
}

void CollectionView::handleCellClicked(int index, bool additive) {
    handleCellClicked(index, additive, false);
}

void CollectionView::handleCellActivated(int index) {
    if (m_onActivate) { m_onActivate(index); }
}

void CollectionView::setOnActivate(std::function<void(int)> callback) {
    m_onActivate = std::move(callback);
}

void CollectionView::handleCellClicked(int index, bool additive, bool range) {
    if (m_selectionMode == CollectionSelectionMode::None) { return; }

    if (m_selectionMode == CollectionSelectionMode::Multiple && range && m_anchorIndex >= 0) {
        selectRange(m_anchorIndex, index);
        m_cursorIndex = index;
    } else if (m_selectionMode == CollectionSelectionMode::Multiple && additive) {
        auto it = std::find(m_selection.begin(), m_selection.end(), index);
        if (it != m_selection.end()) {
            m_selection.erase(it);
        } else {
            m_selection.push_back(index);
        }
        m_anchorIndex = index;
        m_cursorIndex = index;
    } else {
        if (m_selection.size() == 1 && m_selection.front() == index) { return; }
        m_selection.assign(1, index);
        m_anchorIndex = index;
        m_cursorIndex = index;
    }
    applySelectionToVisible();
    if (m_onSelectionChanged) { m_onSelectionChanged(*this); }
}

bool CollectionView::beginCellDrag(int index, DragPayload &payload) {
    if (index < 0 || !m_dataSource.canDragItem || !m_dataSource.canDragItem(index)) { return false; }
    if (!m_dataSource.fillDragPayload) { return false; }
    // Drags the whole multi-selection if the cell is in it; otherwise only the cell itself (the selection
    // is not changed — as in TreeView).
    std::vector<int> indices;
    if (m_selectionMode != CollectionSelectionMode::None && isSelected(index) && m_selection.size() > 1) {
        indices = m_selection;
    } else {
        indices = {index};
    }
    m_dataSource.fillDragPayload(payload, indices);
    payload.source = this;
    return !payload.type.empty();
}

void CollectionView::handleContextMenu(int index, Point position) {
    // A right-click on an unselected item makes it the selection (as in file managers); index<0 — background.
    if (index >= 0 && m_selectionMode != CollectionSelectionMode::None && !isSelected(index)) {
        m_selection.assign(1, index);
        m_anchorIndex = index;
        m_cursorIndex = index;
        applySelectionToVisible();
        if (m_onSelectionChanged) { m_onSelectionChanged(*this); }
    }
    if (m_onContextMenu) { m_onContextMenu(*this, index, position); }
}

void CollectionView::applySelectionToVisible() {
    for (auto &entry : m_visibleCells) { entry.second->setCellSelected(isSelected(entry.first), isFocused()); }
}

void CollectionView::focusChanged(bool) {
    applySelectionToVisible(); // active/inactive selection color
}

void CollectionView::themeChanged() {
    // Visible cells are re-themed by the theme propagating through the tree; pooled cells are not.
    // The pool is cleared so that reused cells do not keep the colors of the previous theme.
    m_recyclePool.clear();
}

bool CollectionView::keyDown(KeyEvent &event) {
    if (m_selectionMode == CollectionSelectionMode::None || m_itemCount == 0) { return false; }
    switch (event.key) {
        case Key::Up: moveSelection(NavDirection::Up, event.isShiftDown); return true;
        case Key::Down: moveSelection(NavDirection::Down, event.isShiftDown); return true;
        case Key::Left: moveSelection(NavDirection::Left, event.isShiftDown); return true;
        case Key::Right: moveSelection(NavDirection::Right, event.isShiftDown); return true;
        default: return false;
    }
}

void CollectionView::selectRange(int anchor, int target) {
    if (anchor < 0 || target < 0 || anchor >= m_itemCount || target >= m_itemCount) {
        m_selection.assign(1, std::clamp(target, 0, std::max(0, m_itemCount - 1)));
        return;
    }
    if (anchor > target) { std::swap(anchor, target); }
    m_selection.clear();
    for (int i = anchor; i <= target; ++i) { m_selection.push_back(i); }
}

void CollectionView::moveSelection(NavDirection direction, bool extend) {
    if (m_selectionMode == CollectionSelectionMode::None || m_itemCount == 0) { return; }
    int current = m_cursorIndex >= 0 ? m_cursorIndex : (m_selection.empty() ? -1 : m_selection.front());
    int next;
    if (current < 0) {
        // no cursor → first step: down/right goes to the first item, up/left to the last one
        const bool forward = direction == NavDirection::Down || direction == NavDirection::Right;
        next = forward ? 0 : m_itemCount - 1;
    } else {
        next = m_layout->nextIndex(current, direction, m_itemCount, getFrame().size); // the layout decides where to
    }
    m_cursorIndex = next;
    scrollIndexIntoView(next); // scrolling realizes the cell (virtualization) and applies the selection
    if (extend && m_selectionMode == CollectionSelectionMode::Multiple) {
        if (m_anchorIndex < 0) { m_anchorIndex = next; }
        selectRange(m_anchorIndex, next);
    } else {
        if (m_selection.size() == 1 && m_selection.front() == next) { return; }
        m_selection.assign(1, next);
        m_anchorIndex = next;
    }
    applySelectionToVisible();
    if (m_onSelectionChanged) { m_onSelectionChanged(*this); }
}

void CollectionView::scrollIndexIntoView(int index) {
    if (!m_layout || index < 0 || index >= m_itemCount) { return; }
    const Size viewport = getFrame().size;
    const Rect itemFrame = m_layout->frameForItem(index, viewport);
    const float top = itemFrame.origin.y;
    const float bottom = top + itemFrame.size.height;
    const float viewTop = getContentOffset().y;
    if (top < viewTop) {
        setContentOffset(Point(0.f, top));
    } else if (bottom > viewTop + viewport.height) {
        setContentOffset(Point(0.f, bottom - viewport.height));
    }
}

} // namespace PandaUI
