#include "PandaUI/Elements/TreeView.hpp"

#include "PandaUI/Core/KeyboardEvent.hpp"
#include "PandaUI/Core/Theme.hpp"
#include "PandaUI/Elements/Control.hpp"
#include "PandaUI/Rendering/UIRenderer.hpp"
#include "PandaUI/Runtime/Context.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace PandaUI {

namespace {
    constexpr float BASE_PAD_LEFT = 8.f;
    constexpr float INDENT = 16.f;
    constexpr float DISCLOSURE_SIZE = 12.f; // width of the arrow (and click) zone
    constexpr float LABEL_GAP = 4.f;
    constexpr float ROW_PADDING_RIGHT = 12.f;

    // Chevron (›/⌄) made of two lines: LONG — the long side of an arm, SHORT — the short one.
    constexpr float CHEVRON_LONG = 3.4f;
    constexpr float CHEVRON_SHORT = 1.8f;
    constexpr float CHEVRON_THICKNESS = 1.6f;

    constexpr double SPRING_OPEN_DELAY = 0.6; // seconds of hovering over a collapsed item before auto-expansion

    constexpr float DRAG_AUTOSCROLL_EDGE = 24.f;   // height of the edge zone (px) where a drag triggers auto-scroll
    constexpr float DRAG_AUTOSCROLL_SPEED = 520.f; // max auto-scroll speed (px/s), right at the edge

    constexpr float OVERSCAN = 220.f; // row realization buffer beyond the visible area (px)
} // namespace

// Tree row: a Control wrapper with a depth indent, a chevron arrow (drawn in draw()) and
// user content. It is reused while scrolling (recycling) — configure() sets everything.
class TreeView::TreeRow : public Control {
public:
    TreeRow(TreeView *tree, std::shared_ptr<View> content)
        : m_tree(tree)
        , m_content(std::move(content)) {
        setCursor(Cursor::PointingHand);
        setFocusable(false); // keys are handled by the tree itself, not by the row
        layout().setPositionType(PositionType::Absolute);
        layout().setPosition(Edge::Left, Length::points(0.f));
        layout().setWidth(Length::percent(100.f));
        layout().setHeight(Length::points(m_tree->m_rowHeight));
        surface().setCornerRadius(0.f); // row selection — no rounding

        // The content does not intercept the pointer — the whole row catches hover/click (otherwise the
        // highlight would appear only over the indent/padding). The content's left edge is set in configure().
        m_content->setUserInteractionEnabled(false);
        m_content->layout().setPositionType(PositionType::Absolute);
        m_content->layout().setPosition(Edge::Top, Length::points(0.f));
        m_content->layout().setPosition(Edge::Bottom, Length::points(0.f));
        m_content->layout().setPosition(Edge::Right, Length::points(ROW_PADDING_RIGHT));
        addSubview(m_content);
    }

    View &content() {
        return *m_content;
    }
    TreeItem item() const {
        return m_item;
    }

    void configure(TreeItem item, int depth, bool expandable, bool expanded, bool selected, bool active, bool dropHighlight) {
        m_item = item;
        m_depth = depth;
        m_expandable = expandable;
        m_expanded = expanded;
        m_selected = selected;
        m_active = active;
        m_dropHighlight = dropHighlight;
        m_content->layout().setPosition(
            Edge::Left, Length::points(BASE_PAD_LEFT + static_cast<float>(depth) * INDENT + DISCLOSURE_SIZE + LABEL_GAP)
        );
        applyStyle();
        setNeedsDisplay();
    }
    void setTop(float y) {
        layout().setPosition(Edge::Top, Length::points(y));
    }
    void setRowSelected(bool selected, bool active) {
        if (m_selected == selected && m_active == active) { return; }
        m_selected = selected;
        m_active = active; // active = the tree is focused → brighter selection
        applyStyle();
    }
    void setDropHighlight(bool highlight) {
        if (m_dropHighlight == highlight) { return; }
        m_dropHighlight = highlight;
        applyStyle();
    }

protected:
    bool pointerDown(PointerEvent &event) override {
        if (event.button == PointerButton::Right) {
            m_tree->handleContextMenu(m_item, event.position);
            return true; // right click — context menu, no selection
        }
        if (m_expandable) {
            const Rect worldFrame = getWorldFrame();
            const float localX = event.position.x - worldFrame.origin.x;
            const float left = BASE_PAD_LEFT + static_cast<float>(m_depth) * INDENT;
            if (localX >= left && localX <= left + DISCLOSURE_SIZE + 2.f) {
                m_tree->handleDisclosureClicked(m_item);
                return true; // click on the arrow — the row is not selected
            }
        }
        m_additiveClick = event.isSuperDown || event.isControlDown;
        m_rangeClick = event.isShiftDown;
        return Control::pointerDown(event);
    }
    bool prepareDrag(DragPayload &payload, Point) override {
        payload.type = m_tree->m_dragType;
        payload.source = m_tree; // source = this tree; reparent only within it
        payload.ghostText = m_tree->beginItemDrag(m_item); // sets m_dragItems
        payload.items = m_tree->dragItemIds();             // node ids → external drop targets (e.g. Inspector)
        return true;
    }
    void performPrimaryAction() override {
        m_tree->handleRowClicked(m_item, m_additiveClick, m_rangeClick);
    }
    void controlStateChanged(ControlState, ControlState) override {
        applyStyle();
    }
    void themeChanged() override {
        applyStyle();
        setNeedsDisplay();
    }
    void draw(float offsetX, float offsetY) override {
        if (!m_expandable) { return; }
        Context *context = getContext();
        if (!context) { return; }
        UIRenderer &renderer = context->getRenderer();

        const Rect frame = getFrame();
        const float cx =
            offsetX + frame.origin.x + BASE_PAD_LEFT + static_cast<float>(m_depth) * INDENT + DISCLOSURE_SIZE * 0.5f;
        const float cy = offsetY + frame.origin.y + frame.size.height * 0.5f;
        const Color color = theme().textDim; // soft selection — the chevron is not inverted

        if (m_expanded) {
            // ⌄ — down
            renderer.drawLine(
                Point(cx - CHEVRON_LONG, cy - CHEVRON_SHORT), Point(cx, cy + CHEVRON_SHORT), CHEVRON_THICKNESS, color
            );
            renderer.drawLine(
                Point(cx, cy + CHEVRON_SHORT), Point(cx + CHEVRON_LONG, cy - CHEVRON_SHORT), CHEVRON_THICKNESS, color
            );
        } else {
            // › — right
            renderer.drawLine(
                Point(cx - CHEVRON_SHORT, cy - CHEVRON_LONG), Point(cx + CHEVRON_SHORT, cy), CHEVRON_THICKNESS, color
            );
            renderer.drawLine(
                Point(cx + CHEVRON_SHORT, cy), Point(cx - CHEVRON_SHORT, cy + CHEVRON_LONG), CHEVRON_THICKNESS, color
            );
        }
    }

private:
    void applyStyle() {
        const Theme &t = theme();
        if (m_selected) {
            setBackgroundColor(m_active ? t.selection : t.selectionInactive); // soft; brighter in focus
        } else if (hasControlState(ControlState::Hovered) || hasControlState(ControlState::Highlighted)) {
            setBackgroundColor(t.rowHover);
        } else {
            setBackgroundColor(Color(0x00000000));
        }
        // "Drop into this node" target: an accent border on top of the background.
        surface().setBorderWidth(m_dropHighlight ? 1.5f : 0.f);
        if (m_dropHighlight) { surface().setBorderColor(t.accent); }
    }

    TreeView *m_tree;
    std::shared_ptr<View> m_content;
    TreeItem m_item = nullptr;
    int m_depth = 0;
    bool m_expandable = false;
    bool m_expanded = false;
    bool m_selected = false;
    bool m_active = false; // the tree is focused (for the active selection color)
    bool m_dropHighlight = false;
    bool m_additiveClick = false;
    bool m_rangeClick = false;
};

/* ------------------------------ TreeView ------------------------------ */

TreeView::TreeView() {
    setScrollDirection(ScrollDirection::Vertical);
    setFocusable(true);             // the tree accepts focus → arrows/Shift work (rows are not focusable)
    setContentSize(Size(0.f, 0.f)); // the content size is computed here from the flat list (virtualization)
}

TreeView::~TreeView() = default;

void TreeView::setDataSource(TreeDataSource dataSource) {
    m_dataSource = std::move(dataSource);
    reloadData();
}

void TreeView::reloadData() {
    m_needsRebuild = true;
    setNeedsDisplay();
}

void TreeView::setRowHeight(float height) {
    if (height <= 0.f || height == m_rowHeight) { return; }
    m_rowHeight = height;
    m_needsRebuild = true;
    setNeedsDisplay();
}

void TreeView::setExpanded(TreeItem item, bool expanded) {
    if (!item) { return; }
    const bool current = m_expanded.count(item) > 0;
    if (current == expanded) { return; }
    if (expanded) {
        m_expanded.insert(item);
    } else {
        m_expanded.erase(item);
    }
    m_needsRebuild = true;
    setNeedsDisplay();
}

bool TreeView::isExpanded(TreeItem item) const {
    return m_expanded.count(item) > 0;
}

void TreeView::setSelectionMode(TreeSelectionMode mode) {
    if (m_selectionMode == mode) { return; }
    m_selectionMode = mode;
    if (mode == TreeSelectionMode::None) {
        m_selection.clear();
    } else if (mode == TreeSelectionMode::Single && m_selection.size() > 1) {
        m_selection.resize(1);
    }
    applySelectionToVisible();
}

bool TreeView::isSelected(TreeItem item) const {
    return std::find(m_selection.begin(), m_selection.end(), item) != m_selection.end();
}

void TreeView::setSelectedItem(TreeItem item) {
    m_selection.clear();
    if (item) { m_selection.push_back(item); }
    m_anchorItem = item;
    m_cursorItem = item;
    applySelectionToVisible();
}

void TreeView::setSelectedItems(const std::vector<TreeItem> &items) {
    m_selection = items;
    m_anchorItem = items.empty() ? nullptr : items.back();
    m_cursorItem = m_anchorItem;
    applySelectionToVisible();
}

void TreeView::setOnSelectionChanged(SelectionChangedCallback callback) {
    m_onSelectionChanged = std::move(callback);
}
void TreeView::setOnContextMenu(ContextMenuCallback callback) {
    m_onContextMenu = std::move(callback);
}
void TreeView::setCanMove(CanMoveCallback callback) {
    m_canMove = std::move(callback);
}
void TreeView::setOnMove(MoveCallback callback) {
    m_onMove = std::move(callback);
}
void TreeView::setDragType(std::string type) {
    m_dragType = std::move(type);
}

/* ------------------------------ Flatten + virtualization ------------------------------ */

void TreeView::rebuildFlat() {
    m_flat.clear();
    if (!m_dataSource.rootItem || !m_dataSource.numberOfChildren || !m_dataSource.childAt) { return; }
    if (TreeItem root = m_dataSource.rootItem()) { flatten(root, 0); }
}

void TreeView::flatten(TreeItem item, int depth) {
    const int childCount = m_dataSource.numberOfChildren(item);
    m_flat.push_back({item, depth, childCount > 0});
    if (childCount > 0 && isExpanded(item)) {
        for (int i = 0; i < childCount; ++i) { flatten(m_dataSource.childAt(item, i), depth + 1); }
    }
}

bool TreeView::viewportChanged() const {
    const Size viewport = getFrame().size;
    return viewport.width != m_lastViewport.width || viewport.height != m_lastViewport.height;
}

void TreeView::update(double deltaTime) {
    ScrollView::update(deltaTime);

    // Spring-loading: the cursor stays over a collapsed item during a drag → expand it.
    if (m_springItem) {
        m_springElapsed += deltaTime;
        if (m_springElapsed >= SPRING_OPEN_DELAY) {
            TreeItem item = m_springItem;
            m_springItem = nullptr;
            m_springElapsed = 0.0;
            setExpanded(item, true);
        }
    }

    // Structural rebuild (reload / expansion): rebuild the flat list and rebind the rows.
    if (m_needsRebuild) {
        m_needsRebuild = false;
        rebuildFlat();
        updateVisibleRows(true);
    }

    // Auto-scroll near the edges during a drag: ticked every frame (the cursor may sit still at the border).
    if (m_dragInside) {
        const Rect view = getWorldFrame();
        const float top = view.origin.y;
        const float bottom = view.origin.y + view.size.height;
        float speed = 0.f;
        if (m_lastDragCursor.y < top + DRAG_AUTOSCROLL_EDGE) {
            const float t = (top + DRAG_AUTOSCROLL_EDGE - m_lastDragCursor.y) / DRAG_AUTOSCROLL_EDGE;
            speed = -DRAG_AUTOSCROLL_SPEED * std::clamp(t, 0.f, 1.f);
        } else if (m_lastDragCursor.y > bottom - DRAG_AUTOSCROLL_EDGE) {
            const float t = (m_lastDragCursor.y - (bottom - DRAG_AUTOSCROLL_EDGE)) / DRAG_AUTOSCROLL_EDGE;
            speed = DRAG_AUTOSCROLL_SPEED * std::clamp(t, 0.f, 1.f);
        }
        if (speed != 0.f) {
            const Point before = getContentOffset();
            setContentOffset(Point(before.x, before.y + speed * static_cast<float>(deltaTime)));
            if (getContentOffset().y != before.y) {
                updateVisibleRows(false);              // show the rows that have scrolled in
                updateDropForCursor(m_lastDragCursor); // rows moved under the still cursor — recompute the target
            }
        }
    }

    // Realize the visible rows for the current scroll/viewport (regular scrolling, resize, first layout).
    if (viewportChanged() || getContentOffset().y != m_lastOffsetY) { updateVisibleRows(viewportChanged()); }
}

void TreeView::updateVisibleRows(bool forceRebind) {
    const Size viewport = getFrame().size;
    if (viewport.height <= 0.f) { return; } // not laid out yet

    if (forceRebind) {
        setContentSize(Size(viewport.width, static_cast<float>(m_flat.size()) * m_rowHeight));
        for (auto &entry : m_visibleRows) { recycle(entry.second); }
        m_visibleRows.clear();
    }
    m_lastViewport = viewport;
    m_lastOffsetY = getContentOffset().y;

    if (m_flat.empty() || !m_dataSource.configureCell) { return; }

    const float visibleTop = m_lastOffsetY - OVERSCAN;
    const float visibleBottom = m_lastOffsetY + viewport.height + OVERSCAN;
    int first = std::max(0, static_cast<int>(std::floor(visibleTop / m_rowHeight)));
    int last = std::min(static_cast<int>(m_flat.size()) - 1, static_cast<int>(std::floor(visibleBottom / m_rowHeight)));

    // Recycle the rows outside the range.
    for (auto it = m_visibleRows.begin(); it != m_visibleRows.end();) {
        if (it->first < first || it->first > last) {
            recycle(it->second);
            it = m_visibleRows.erase(it);
        } else {
            ++it;
        }
    }
    // Realize the missing ones within the range.
    for (int i = first; i <= last; ++i) {
        if (m_visibleRows.find(i) != m_visibleRows.end()) { continue; }
        const FlatItem &fi = m_flat[i];
        std::shared_ptr<TreeRow> row = acquireRow();
        row->configure(
            fi.item, fi.depth, fi.expandable, isExpanded(fi.item), isSelected(fi.item), isFocused(), i == m_dropHighlightIndex
        );
        m_dataSource.configureCell(row->content(), fi.item, fi.depth);
        row->setTop(static_cast<float>(i) * m_rowHeight);
        addContentSubview(row);
        m_visibleRows[i] = row;
    }
}

std::shared_ptr<TreeView::TreeRow> TreeView::acquireRow() {
    if (!m_recyclePool.empty()) {
        std::shared_ptr<TreeRow> row = m_recyclePool.back();
        m_recyclePool.pop_back();
        return row;
    }
    std::shared_ptr<View> content = m_dataSource.cellForReuse ? m_dataSource.cellForReuse() : std::make_shared<View>();
    return std::make_shared<TreeRow>(this, std::move(content));
}

void TreeView::recycle(const std::shared_ptr<TreeRow> &row) {
    getContentView()->removeSubview(row);
    m_recyclePool.push_back(row);
}

int TreeView::flatIndexOf(TreeItem item) const {
    for (int i = 0; i < static_cast<int>(m_flat.size()); ++i) {
        if (m_flat[i].item == item) { return i; }
    }
    return -1;
}

int TreeView::subtreeEndIndex(int flatIndex) const {
    if (flatIndex < 0 || flatIndex >= static_cast<int>(m_flat.size())) { return flatIndex; }
    const int depth = m_flat[flatIndex].depth;
    int end = flatIndex;
    for (int i = flatIndex + 1; i < static_cast<int>(m_flat.size()) && m_flat[i].depth > depth; ++i) { end = i; }
    return end;
}

/* ------------------------------ Selection / context ------------------------------ */

void TreeView::handleRowClicked(TreeItem item, bool additive, bool range) {
    if (m_selectionMode == TreeSelectionMode::None) { return; }

    if (m_selectionMode == TreeSelectionMode::Multiple && range && m_anchorItem) {
        // Shift — select the range from the anchor to the item; the anchor stays, the cursor moves to the item.
        selectRange(m_anchorItem, item);
        m_cursorItem = item;
    } else if (m_selectionMode == TreeSelectionMode::Multiple && additive) {
        // ⌘/Ctrl — toggle the item in the set; it becomes the anchor/cursor.
        auto it = std::find(m_selection.begin(), m_selection.end(), item);
        if (it != m_selection.end()) {
            m_selection.erase(it);
        } else {
            m_selection.push_back(item);
        }
        m_anchorItem = item;
        m_cursorItem = item;
    } else {
        // Plain click — select exactly one; it is the new anchor/cursor.
        m_selection.assign(1, item);
        m_anchorItem = item;
        m_cursorItem = item;
    }
    applySelectionToVisible();
    if (m_onSelectionChanged) { m_onSelectionChanged(*this); }
}

void TreeView::handleDisclosureClicked(TreeItem item) {
    setExpanded(item, !isExpanded(item));
}

void TreeView::handleContextMenu(TreeItem item, Point position) {
    // A right click on an unselected item makes it the selection (as in file managers).
    if (item && m_selectionMode != TreeSelectionMode::None && !isSelected(item)) {
        m_selection.assign(1, item);
        m_anchorItem = item;
        m_cursorItem = item;
        applySelectionToVisible();
        if (m_onSelectionChanged) { m_onSelectionChanged(*this); }
    }
    if (m_onContextMenu) { m_onContextMenu(*this, item, position); }
}

void TreeView::applySelectionToVisible() {
    for (auto &entry : m_visibleRows) { entry.second->setRowSelected(isSelected(entry.second->item()), isFocused()); }
}

void TreeView::focusChanged(bool) {
    applySelectionToVisible(); // active/inactive selection color
}

void TreeView::themeChanged() {
    // Visible rows are re-themed by the theme propagating through the tree; pooled rows are not.
    // The pool is cleared so that reused rows do not keep the colors of the previous theme.
    m_recyclePool.clear();
}

bool TreeView::pointerDown(PointerEvent &event) {
    // A click gets here only when it misses the rows (the Control rows consume it), i.e. on an empty area.
    // A right click on an empty area → a context menu with an empty item (e.g. "create in the root").
    if (event.button == PointerButton::Right) {
        handleContextMenu(nullptr, event.position);
        return true;
    }
    // A left click on an empty area — clear the selection (as in file managers/NSOutlineView). Then it
    // is passed on to the base ScrollView to preserve drag-to-scroll.
    if (m_selectionMode != TreeSelectionMode::None && !m_selection.empty()) {
        m_selection.clear();
        m_anchorItem = nullptr;
        m_cursorItem = nullptr;
        applySelectionToVisible();
        if (m_onSelectionChanged) { m_onSelectionChanged(*this); }
    }
    return ScrollView::pointerDown(event);
}

/* ------------------------------ Keyboard navigation ------------------------------ */

TreeItem TreeView::currentItem() const {
    return m_cursorItem ? m_cursorItem : selectedItem();
}

void TreeView::selectSingle(TreeItem item) {
    m_selection.assign(1, item);
    m_anchorItem = item;
    m_cursorItem = item;
    const int idx = flatIndexOf(item);
    if (idx >= 0) { scrollItemIntoView(idx); }
    applySelectionToVisible();
    if (m_onSelectionChanged) { m_onSelectionChanged(*this); }
}

void TreeView::selectRange(TreeItem anchor, TreeItem target) {
    int a = flatIndexOf(anchor);
    int b = flatIndexOf(target);
    if (a < 0 || b < 0) {
        m_selection.assign(1, target);
        return;
    }
    if (a > b) { std::swap(a, b); }
    m_selection.clear();
    for (int i = a; i <= b; ++i) { m_selection.push_back(m_flat[i].item); }
}

void TreeView::moveCursor(int delta, bool extend) {
    if (m_flat.empty()) { return; }
    int idx = flatIndexOf(currentItem());
    if (idx < 0) { idx = delta > 0 ? -1 : static_cast<int>(m_flat.size()); } // first step → 0 or the last one
    const int next = std::clamp(idx + delta, 0, static_cast<int>(m_flat.size()) - 1);
    m_cursorItem = m_flat[next].item;

    if (extend && m_selectionMode == TreeSelectionMode::Multiple) {
        selectRange(m_anchorItem ? m_anchorItem : m_cursorItem, m_cursorItem);
    } else {
        m_selection.assign(1, m_cursorItem);
        m_anchorItem = m_cursorItem;
    }
    scrollItemIntoView(next);
    applySelectionToVisible();
    if (m_onSelectionChanged) { m_onSelectionChanged(*this); }
}

void TreeView::collapseOrMoveToParent() {
    const TreeItem item = currentItem();
    const int idx = flatIndexOf(item);
    if (idx < 0) { return; }
    if (m_flat[idx].expandable && isExpanded(item)) {
        setExpanded(item, false); // expanded → collapse
        return;
    }
    // otherwise — go to the parent: the nearest previous row of a smaller depth
    const int depth = m_flat[idx].depth;
    for (int i = idx - 1; i >= 0; --i) {
        if (m_flat[i].depth == depth - 1) {
            selectSingle(m_flat[i].item);
            return;
        }
    }
}

void TreeView::expandOrMoveToChild() {
    const TreeItem item = currentItem();
    const int idx = flatIndexOf(item);
    if (idx < 0) { return; }
    if (m_flat[idx].expandable && !isExpanded(item)) {
        setExpanded(item, true); // collapsed → expand
        return;
    }
    // otherwise — go to the first child (the next row of a greater depth)
    if (idx + 1 < static_cast<int>(m_flat.size()) && m_flat[idx + 1].depth == m_flat[idx].depth + 1) {
        selectSingle(m_flat[idx + 1].item);
    }
}

void TreeView::scrollItemIntoView(int flatIndex) {
    const float rowTop = static_cast<float>(flatIndex) * m_rowHeight;
    const float rowBottom = rowTop + m_rowHeight;
    const float viewTop = getContentOffset().y;
    const float viewHeight = getFrame().size.height;
    if (rowTop < viewTop) {
        setContentOffset(Point(0.f, rowTop));
    } else if (rowBottom > viewTop + viewHeight) {
        setContentOffset(Point(0.f, rowBottom - viewHeight));
    }
}

bool TreeView::keyDown(KeyEvent &event) {
    if (m_selectionMode == TreeSelectionMode::None || m_flat.empty()) { return false; }
    switch (event.key) {
        case Key::Up: moveCursor(-1, event.isShiftDown); return true;
        case Key::Down: moveCursor(1, event.isShiftDown); return true;
        case Key::Left: collapseOrMoveToParent(); return true;
        case Key::Right: expandOrMoveToChild(); return true;
        default: return false;
    }
}

/* ------------------------------ Drag and drop ------------------------------ */

std::string TreeView::beginItemDrag(TreeItem item) {
    if (m_selectionMode != TreeSelectionMode::None && isSelected(item) && m_selection.size() > 1) {
        m_dragItems = m_selection;
    } else {
        m_dragItems = {item};
    }
    if (m_dragItems.size() == 1) {
        return m_dataSource.titleForItem ? m_dataSource.titleForItem(item) : "1 item";
    }
    return std::to_string(m_dragItems.size()) + " items";
}

std::vector<std::uint64_t> TreeView::dragItemIds() const {
    std::vector<std::uint64_t> ids;
    if (!m_dataSource.idForItem) { return ids; }
    ids.reserve(m_dragItems.size());
    for (TreeItem item : m_dragItems) { ids.push_back(m_dataSource.idForItem(item)); }
    return ids;
}

bool TreeView::canAcceptDrop(const DragInfo &drag) {
    return drag.payload.type == m_dragType && drag.payload.source == this && !m_dragItems.empty();
}

TreeView::DropTarget TreeView::computeDropTarget(Point windowPosition) const {
    DropTarget target;

    // The row under the cursor → its item = the new parent; the empty zone below → the root.
    int idx = -1;
    for (const auto &entry : m_visibleRows) {
        const Rect wf = entry.second->getWorldFrame();
        if (windowPosition.y >= wf.origin.y && windowPosition.y < wf.origin.y + wf.size.height) {
            idx = entry.first;
            break;
        }
    }

    if (idx < 0) { // the empty zone below the rows → into the tree root
        target.parent = m_dataSource.rootItem ? m_dataSource.rootItem() : nullptr;
        target.flatIndex = -1;
        target.valid = !m_dragItems.empty() && target.parent != nullptr;
        return target;
    }

    // Dropping onto the dragged item itself or into its subtree is not allowed (otherwise a cycle).
    for (TreeItem dragged : m_dragItems) {
        const int d = flatIndexOf(dragged);
        if (d >= 0 && idx >= d && idx <= subtreeEndIndex(d)) {
            target.valid = false;
            return target;
        }
    }
    target.parent = m_flat[idx].item;
    target.flatIndex = idx;
    target.valid = true;
    return target;
}

void TreeView::applyDropHighlight(int flatIndex) {
    if (flatIndex == m_dropHighlightIndex) { return; }
    auto previous = m_visibleRows.find(m_dropHighlightIndex);
    if (previous != m_visibleRows.end()) { previous->second->setDropHighlight(false); }
    m_dropHighlightIndex = flatIndex;
    auto current = m_visibleRows.find(m_dropHighlightIndex);
    if (current != m_visibleRows.end()) { current->second->setDropHighlight(true); }
    setNeedsDisplay();
}

void TreeView::updateDropForCursor(Point windowPosition) {
    DropTarget target = computeDropTarget(windowPosition);
    if (target.valid && m_canMove && !m_canMove(m_dragItems, target.parent)) { target.valid = false; }

    applyDropHighlight(target.valid ? target.flatIndex : -1);

    // Spring-loading: over a collapsed expandable item the auto-expand timer is started.
    TreeItem candidate = nullptr;
    if (target.valid && target.flatIndex >= 0) {
        const FlatItem &fi = m_flat[target.flatIndex];
        if (fi.expandable && !isExpanded(fi.item)) { candidate = fi.item; }
    }
    if (candidate != m_springItem) {
        m_springItem = candidate;
        m_springElapsed = 0.0;
    }
}

void TreeView::dragUpdated(const DragInfo &drag) {
    m_dragInside = true;
    m_lastDragCursor = drag.windowPosition;
    updateDropForCursor(drag.windowPosition);
}

void TreeView::dragExited() {
    clearDrag();
}

void TreeView::clearDrag() {
    applyDropHighlight(-1);
    m_dragItems.clear();
    m_springItem = nullptr;
    m_springElapsed = 0.0;
    m_dragInside = false;
    setNeedsDisplay();
}

bool TreeView::performDrop(const DragInfo &drag) {
    DropTarget target = computeDropTarget(drag.windowPosition);
    if (target.valid && m_canMove && !m_canMove(m_dragItems, target.parent)) { target.valid = false; }

    // Top-level ones only: an item whose ancestor is also being dragged moves along with it.
    std::vector<TreeItem> items;
    for (TreeItem dragged : m_dragItems) {
        const int d = flatIndexOf(dragged);
        bool covered = false;
        for (TreeItem other : m_dragItems) {
            if (other == dragged) { continue; }
            const int o = flatIndexOf(other);
            if (o >= 0 && d > o && d <= subtreeEndIndex(o)) {
                covered = true;
                break;
            }
        }
        if (!covered) { items.push_back(dragged); }
    }
    const TreeItem parent = target.parent;
    const bool ok = target.valid && !items.empty();
    clearDrag();
    if (!ok) { return false; }

    if (m_onMove) { m_onMove(items, parent); } // the application changes the model and calls reloadData()
    if (parent) { setExpanded(parent, true); } // expand the target to make the moved items visible
    return true;
}

} // namespace PandaUI
