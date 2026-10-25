#pragma once

#include "PandaUI/Elements/ScrollView.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace PandaUI {

// Opaque handle of a tree item (for example, an Entity). In TreeDataSource callbacks it is always non-null:
// the single tree root is obtained through rootItem(). TreeView does NOT store the structure — the items
// are owned by the application (single source of truth), while expansion/selection are keyed by item
// identity and survive reloadData().
using TreeItem = void *;

// Hierarchical data source (NSOutlineView style, but with a single explicit root — without a
// null sentinel). The tree is a projection of the application model.
struct TreeDataSource {
    std::function<TreeItem()> rootItem;                          // the single root (not null)
    std::function<int(TreeItem item)> numberOfChildren;          // number of children of item (item != null)
    std::function<TreeItem(TreeItem item, int index)> childAt;   // index-th child of item (item != null)
    std::function<std::shared_ptr<View>()> cellForReuse;         // create an empty content cell
    std::function<void(View &cell, TreeItem item, int depth)> configureCell; // bind the data
    std::function<std::string(TreeItem item)> titleForItem;      // opt.: label of the drag "ghost"
    std::function<std::uint64_t(TreeItem item)> idForItem;       // opt.: item id → payload.items (external drop targets)
};

enum class TreeSelectionMode {
    None,     // rows cannot be selected
    Single,   // at most one selected row
    Multiple, // ⌘/Ctrl-click toggles within the set
};

// Scrollable tree with virtualization: the expanded visible items are unrolled into a flat list of
// uniform height, and only the rows within the window (+buffer) actually exist — the rest are in the pool.
// Drag-and-drop reparent is handed to the application as an intent (canMove/onMove): TreeView mutates
// nothing itself; the application changes the model and calls reloadData().
class TreeView : public ScrollView {
public:
    using SelectionChangedCallback = std::function<void(TreeView &)>;
    using ContextMenuCallback = std::function<void(TreeView &, TreeItem item, Point position)>;
    using CanMoveCallback = std::function<bool(const std::vector<TreeItem> &items, TreeItem newParent)>;
    using MoveCallback = std::function<void(const std::vector<TreeItem> &items, TreeItem newParent)>;

    TreeView();
    ~TreeView() override;

    void setDataSource(TreeDataSource dataSource);
    void reloadData(); // re-read the structure from the data source (after model changes)

    // Row height (34 by default). Uniform for all rows; set it before populating with data.
    void setRowHeight(float height);

    // Expansion — UI state keyed by item identity.
    void setExpanded(TreeItem item, bool expanded);
    bool isExpanded(TreeItem item) const;

    // Selection — by item identity.
    void setSelectionMode(TreeSelectionMode mode);
    TreeSelectionMode selectionMode() const {
        return m_selectionMode;
    }
    TreeItem selectedItem() const {
        return m_selection.empty() ? nullptr : m_selection.front();
    }
    const std::vector<TreeItem> &selectedItems() const {
        return m_selection;
    }
    bool isSelected(TreeItem item) const;
    void setSelectedItem(TreeItem item);
    void setSelectedItems(const std::vector<TreeItem> &items); // programmatic multi-selection (no onSelectionChanged)
    void setOnSelectionChanged(SelectionChangedCallback callback);
    void setOnContextMenu(ContextMenuCallback callback);

    // Drag-and-drop reparent within the tree. Applying it to the model is up to the application.
    void setCanMove(CanMoveCallback callback);
    void setOnMove(MoveCallback callback);
    // The payload.type tag of the dragged nodes (compatibility key for external drop targets, e.g.
    // a reference field in the inspector). "treeview.nodes" by default; Hierarchy sets, e.g., "entity".
    void setDragType(std::string type);

    void update(double deltaTime) override;
    bool keyDown(KeyEvent &event) override;     // ↑/↓ — navigation, ←/→ — collapse/expand, Shift+↑/↓ — range
    bool pointerDown(PointerEvent &event) override; // click on an empty area (not on a row) — clear the selection

    bool canAcceptDrop(const DragInfo &drag) override;
    void dragUpdated(const DragInfo &drag) override;
    void dragExited() override;
    bool performDrop(const DragInfo &drag) override;

protected:
    void focusChanged(bool focused) override; // active/inactive selection color
    void themeChanged() override;             // clears the reuse pool (old theme colors)

private:
    class TreeRow;
    friend class TreeRow;

    // Flat visible item: an expanded subtree unrolled into a linear list of uniform height.
    struct FlatItem {
        TreeItem item;
        int depth;
        bool expandable;
    };

    struct DropTarget {
        bool valid = false;
        TreeItem parent = nullptr; // new parent (nullptr — the root)
        int flatIndex = -1;        // target row for the highlight (or -1)
    };

    void rebuildFlat();                  // rebuild m_flat from the data source + m_expanded (O(visible))
    void flatten(TreeItem item, int depth);
    void updateVisibleRows(bool forceRebind); // virtualization: realize only the visible rows
    std::shared_ptr<TreeRow> acquireRow();
    void recycle(const std::shared_ptr<TreeRow> &row);
    bool viewportChanged() const;

    int flatIndexOf(TreeItem item) const;
    int subtreeEndIndex(int flatIndex) const; // last index of the subtree of row flatIndex

    void handleRowClicked(TreeItem item, bool additive, bool range);
    void handleDisclosureClicked(TreeItem item);
    void handleContextMenu(TreeItem item, Point position);
    void applySelectionToVisible();
    void selectSingle(TreeItem item);                   // select exactly one + make it the cursor/anchor + reveal it
    void selectRange(TreeItem anchor, TreeItem target); // select a range in the flat list [anchor..target]
    TreeItem currentItem() const;                       // keyboard cursor (or the first selected item)
    void moveCursor(int delta, bool extend);            // ↑/↓ (extend=Shift extends the range from the anchor)
    void collapseOrMoveToParent();                      // ←
    void expandOrMoveToChild();                         // →
    void scrollItemIntoView(int flatIndex);

    std::string beginItemDrag(TreeItem item);
    std::vector<std::uint64_t> dragItemIds() const; // ids of the dragged nodes → payload.items
    DropTarget computeDropTarget(Point windowPosition) const;
    void applyDropHighlight(int flatIndex);
    void updateDropForCursor(Point windowPosition);
    void clearDrag();

    TreeDataSource m_dataSource;
    std::vector<FlatItem> m_flat;            // flat visible list (cache; rebuilt on structural changes)
    std::unordered_set<TreeItem> m_expanded; // expanded items
    std::vector<TreeItem> m_selection;       // selected items
    TreeItem m_anchorItem = nullptr;         // range anchor (Shift selection)
    TreeItem m_cursorItem = nullptr;         // keyboard navigation cursor

    std::unordered_map<int, std::shared_ptr<TreeRow>> m_visibleRows; // flatIndex → realized row
    std::vector<std::shared_ptr<TreeRow>> m_recyclePool;

    TreeSelectionMode m_selectionMode = TreeSelectionMode::Single;
    SelectionChangedCallback m_onSelectionChanged;
    ContextMenuCallback m_onContextMenu;
    CanMoveCallback m_canMove;
    MoveCallback m_onMove;

    bool m_needsRebuild = true; // rebuild the flat list and rebind
    float m_lastOffsetY = -1.f;
    Size m_lastViewport = Size(-1.f, -1.f);
    float m_rowHeight = 30.f; // row height (uniform); changed through setRowHeight

    // Drag-and-drop.
    std::string m_dragType = "treeview.nodes"; // payload.type of the dragged nodes
    std::vector<TreeItem> m_dragItems;
    int m_dropHighlightIndex = -1;
    TreeItem m_springItem = nullptr; // collapsed item under the cursor — auto-expands on a timer
    double m_springElapsed = 0.0;
    bool m_dragInside = false; // the cursor is over the tree → auto-scroll near the edges
    Point m_lastDragCursor;
};

} // namespace PandaUI
