#pragma once

#include "PandaUI/Core/Appearance.hpp"

#include "PandaUI/Elements/CollectionViewLayout.hpp"
#include "PandaUI/Elements/ScrollView.hpp"

#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace PandaUI {

// CollectionView data source (in the spirit of the UIKit data source).
//  - numberOfItems: how many items there are in total;
//  - cellForReuse:  create an EMPTY cell content (called only when the reuse pool is empty);
//  - configureCell: bind the data to the cell for an index (called on show and on reuse —
//                   must set the content completely, since the cell may have shown another item).
struct CollectionViewDataSource {
    std::function<int()> numberOfItems;
    std::function<std::shared_ptr<View>()> cellForReuse;
    std::function<void(View &cell, int index)> configureCell;
    // Drag source (optional): canDragItem(index) allows dragging a cell; fillDragPayload receives
    // the dragged indices (the whole multi-selection if the cell is part of it, otherwise only the
    // cell itself) and fills type/items/ghostText. The collection sets source itself. Without these
    // callbacks cells cannot be dragged.
    std::function<bool(int index)> canDragItem;
    std::function<void(DragPayload &payload, const std::vector<int> &indices)> fillDragPayload;
    // Prefetch (optional, in the spirit of UIKit UICollectionViewDataSourcePrefetching): prefetchItems
    // is called for indices that entered the buffer zone around the visible range (to prepare data
    // ahead of time); cancelPrefetching — for those that left it (only pending work is worth
    // canceling).
    std::function<void(const std::vector<int> &indices)> prefetchItems;
    std::function<void(const std::vector<int> &indices)> cancelPrefetching;
};

enum class CollectionSelectionMode {
    None,     // cells are not selected (pure display, e.g. console)
    Single,   // at most one selected
    Multiple, // ⌘/Ctrl-click toggles within the set
};

// Scrollable collection with virtualization and cell reuse: only the visible cells (+ a buffer)
// actually exist, the rest sit in the pool. The layout is defined by CollectionViewLayout
// (list/grid), the data by CollectionViewDataSource. Built on top of ScrollView (vertical scrolling).
// The look of a collection: the selection and hover plates of the cells.
struct CollectionViewAppearance {
    float cellRadius = 0.f; // cell selection — no rounding
    ThemeColor selection = ThemeColor::selection; // when the collection is focused
    ThemeColor selectionInactive = ThemeColor::selectionInactive;
    ThemeColor hover = ThemeColor::rowHover;
    SurfaceImage selectionImage;
};

class CollectionView : public ScrollView {
public:
    using SelectionChangedCallback = std::function<void(CollectionView &)>;
    using ContextMenuCallback = std::function<void(CollectionView &, int index, Point position)>; // index<0 — backdrop

    explicit CollectionView(std::shared_ptr<CollectionViewLayout> layout);
    ~CollectionView() override;

    // The look of the collections created from now on (Appearance.hpp).
    static CollectionViewAppearance &defaultAppearance();
    void setAppearance(const CollectionViewAppearance &appearance);
    const CollectionViewAppearance &appearance() const {
        return m_appearance;
    }

    void setDataSource(CollectionViewDataSource dataSource);
    void setLayout(std::shared_ptr<CollectionViewLayout> layout);
    void reloadData();     // re-read count, recompute sizes, rebind the visible cells
    void reconfigureItem(int index); // rebind ONE visible cell (the item's data was updated)
    void scrollToBottom(); // scroll to the end (e.g. console after adding rows)

    int itemCount() const {
        return m_itemCount;
    }

    void setSelectionMode(CollectionSelectionMode mode);
    CollectionSelectionMode selectionMode() const {
        return m_selectionMode;
    }
    const std::vector<int> &selectedIndices() const {
        return m_selection;
    }
    void setSelectedIndices(std::vector<int> indices);
    bool isSelected(int index) const;
    void clearSelection(); // clear the selection (e.g. when the data set changes)
    void setOnSelectionChanged(SelectionChangedCallback callback);
    void setOnContextMenu(ContextMenuCallback callback); // right-click on a cell (index) or the background (index<0)
    void setOnActivate(std::function<void(int index)> callback); // double-click on a cell

    void update(double deltaTime) override;
    bool keyDown(KeyEvent &event) override; // ↑/↓ — change the selection (when the collection is focused)

protected:
    bool pointerDown(PointerEvent &event) override; // right-click on an empty grid area → context menu (index<0)
    void focusChanged(bool focused) override; // active/inactive selection color
    void themeChanged() override;             // clears the reuse pool (old theme colors)

private:
    class Cell;
    friend class Cell;

    void updateVisibleCells(bool forceRebuild);
    std::shared_ptr<Cell> acquireCell();
    void recycle(const std::shared_ptr<Cell> &cell);
    void handleCellClicked(int index, bool additive);
    void handleCellClicked(int index, bool additive, bool range);
    void handleCellActivated(int index); // double-click → m_onActivate
    bool beginCellDrag(int index, DragPayload &payload); // drag source: build the payload (multi-selection-aware)
    void handleContextMenu(int index, Point position); // selects the item (if index>=0) and calls the callback
    void applySelectionToVisible();
    void selectRange(int anchor, int target);
    void moveSelection(NavDirection direction, bool extend); // arrow-key navigation (the layout decides where to)
    void scrollIndexIntoView(int index); // scroll so that the item at index is visible
    bool viewportChanged() const;
    void updatePrefetch(int firstVisible, int lastVisible); // buffer zone diff → prefetch/cancel

    CollectionViewAppearance m_appearance;
    std::shared_ptr<CollectionViewLayout> m_layout;
    CollectionViewDataSource m_dataSource;
    int m_itemCount = 0;

    std::unordered_map<int, std::shared_ptr<Cell>> m_visibleCells; // index → visible cell
    std::vector<std::shared_ptr<Cell>> m_recyclePool;              // free cells available for reuse

    CollectionSelectionMode m_selectionMode = CollectionSelectionMode::None;
    std::vector<int> m_selection;
    int m_anchorIndex = -1;
    int m_cursorIndex = -1;
    SelectionChangedCallback m_onSelectionChanged;
    std::function<void(int)> m_onActivate;
    ContextMenuCallback m_onContextMenu;

    bool m_needsReload = true;        // full rebuild is deferred to update() (the viewport size is valid there)
    float m_lastOffsetY = -1.f;       // to detect scrolling between frames
    Size m_lastViewport = Size(-1.f, -1.f); // to detect a resize (the grid will be laid out again)
    std::vector<int> m_prefetchedIndices;   // current prefetch buffer zone (sorted)
};

} // namespace PandaUI
