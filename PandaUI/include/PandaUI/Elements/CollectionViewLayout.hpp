#pragma once

#include "PandaUI/Core/Base.hpp"

#include <functional>
#include <vector>

namespace PandaUI {

// Direction of keyboard navigation across the items (arrow keys).
enum class NavDirection { Up, Down, Left, Right };

// CollectionView layout strategy. All three geometry methods are analytic (O(1)), so virtualization
// is cheap: the index range is known immediately from the visible rectangle. The layout also knows
// the spatial navigation (arrow keys), since it knows the rows/columns. Scrolling is vertical.
class CollectionViewLayout {
public:
    virtual ~CollectionViewLayout() = default;

    // Full content size for itemCount items at the given viewport size.
    virtual Size contentSize(int itemCount, Size viewport) const = 0;
    // Frame of the item at index in content coordinates.
    virtual Rect frameForItem(int index, Size viewport) const = 0;
    // Range of indices [firstOut..lastOut] whose frames intersect visible (content coordinates).
    // lastOut < firstOut means "empty".
    virtual void itemRange(Rect visible, int itemCount, Size viewport, int &firstOut, int &lastOut) const = 0;
    // Target index for arrow-key navigation from current, accounting for the layout geometry
    // (clamped to [0..count-1]).
    virtual int nextIndex(int current, NavDirection direction, int itemCount, Size viewport) const = 0;
};

// List: rows of equal height spanning the whole viewport width.
class ListCollectionLayout final : public CollectionViewLayout {
public:
    explicit ListCollectionLayout(float rowHeight, float spacing = 0.f);

    Size contentSize(int itemCount, Size viewport) const override;
    Rect frameForItem(int index, Size viewport) const override;
    void itemRange(Rect visible, int itemCount, Size viewport, int &firstOut, int &lastOut) const override;
    int nextIndex(int current, NavDirection direction, int itemCount, Size viewport) const override;

private:
    float m_rowHeight;
    float m_spacing;
};

// Grid: cells of equal size, wrapping at the viewport width, scrolling downward.
class GridCollectionLayout final : public CollectionViewLayout {
public:
    GridCollectionLayout(Size itemSize, float spacing = 8.f, float padding = 8.f);

    Size contentSize(int itemCount, Size viewport) const override;
    Rect frameForItem(int index, Size viewport) const override;
    void itemRange(Rect visible, int itemCount, Size viewport, int &firstOut, int &lastOut) const override;
    int nextIndex(int current, NavDirection direction, int itemCount, Size viewport) const override;

private:
    int columnsFor(Size viewport) const;

    Size m_itemSize;
    float m_spacing;
    float m_padding;
};

// List with variable-height rows: the height is provided by a measure callback (index, width for
// the viewport). The cache of heights and prefix sums is recomputed when count/width change;
// invalidate() — when the data changes (e.g. the list's reloadData).
class VariableListCollectionLayout final : public CollectionViewLayout {
public:
    using MeasureFn = std::function<float(int index, float width)>;

    explicit VariableListCollectionLayout(MeasureFn measure, float spacing = 0.f);

    void invalidate();

    Size contentSize(int itemCount, Size viewport) const override;
    Rect frameForItem(int index, Size viewport) const override;
    void itemRange(Rect visible, int itemCount, Size viewport, int &firstOut, int &lastOut) const override;
    int nextIndex(int current, NavDirection direction, int itemCount, Size viewport) const override;

private:
    void ensure(int itemCount, Size viewport) const;

    MeasureFn m_measure;
    float m_spacing;
    mutable std::vector<float> m_offsets; // offsets[i] — Y of the start of row i; size count+1
    mutable float m_cachedWidth = -1.f;
    mutable int m_cachedCount = -1;
};

} // namespace PandaUI
