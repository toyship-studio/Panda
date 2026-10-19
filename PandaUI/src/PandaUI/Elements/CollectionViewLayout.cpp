#include "PandaUI/Elements/CollectionViewLayout.hpp"

#include <algorithm>
#include <cmath>

namespace PandaUI {

/* ------------------------------ ListCollectionLayout ------------------------------ */

ListCollectionLayout::ListCollectionLayout(float rowHeight, float spacing)
    : m_rowHeight(rowHeight)
    , m_spacing(spacing) {}

Size ListCollectionLayout::contentSize(int itemCount, Size viewport) const {
    if (itemCount <= 0) { return Size(viewport.width, 0.f); }
    return Size(viewport.width, itemCount * m_rowHeight + (itemCount - 1) * m_spacing);
}

Rect ListCollectionLayout::frameForItem(int index, Size viewport) const {
    const float stride = m_rowHeight + m_spacing;
    return Rect(0.f, index * stride, viewport.width, m_rowHeight);
}

void ListCollectionLayout::itemRange(Rect visible, int itemCount, Size, int &firstOut, int &lastOut) const {
    const float stride = m_rowHeight + m_spacing;
    if (stride <= 0.f || itemCount <= 0) {
        firstOut = 0;
        lastOut = -1;
        return;
    }
    firstOut = static_cast<int>(std::floor(visible.origin.y / stride));
    lastOut = static_cast<int>(std::floor((visible.origin.y + visible.size.height) / stride));
}

int ListCollectionLayout::nextIndex(int current, NavDirection direction, int itemCount, Size) const {
    if (itemCount <= 0) { return -1; }
    int next = current;
    if (direction == NavDirection::Up) {
        next = current - 1;
    } else if (direction == NavDirection::Down) {
        next = current + 1;
    } // Left/Right — a single-column list has no horizontal axis, stay in place
    return std::clamp(next, 0, itemCount - 1);
}

/* ------------------------------ GridCollectionLayout ------------------------------ */

GridCollectionLayout::GridCollectionLayout(Size itemSize, float spacing, float padding)
    : m_itemSize(itemSize)
    , m_spacing(spacing)
    , m_padding(padding) {}

int GridCollectionLayout::columnsFor(Size viewport) const {
    const float stride = m_itemSize.width + m_spacing;
    if (stride <= 0.f) { return 1; }
    // Available width + m_spacing: the last column carries no trailing spacing.
    const float available = viewport.width - 2.f * m_padding + m_spacing;
    return std::max(1, static_cast<int>(std::floor(available / stride)));
}

Size GridCollectionLayout::contentSize(int itemCount, Size viewport) const {
    if (itemCount <= 0) { return Size(viewport.width, 0.f); }
    const int columns = columnsFor(viewport);
    const int rows = (itemCount + columns - 1) / columns;
    const float height = 2.f * m_padding + rows * m_itemSize.height + (rows - 1) * m_spacing;
    return Size(viewport.width, height);
}

Rect GridCollectionLayout::frameForItem(int index, Size viewport) const {
    const int columns = columnsFor(viewport);
    const int column = index % columns;
    const int row = index / columns;
    const float x = m_padding + column * (m_itemSize.width + m_spacing);
    const float y = m_padding + row * (m_itemSize.height + m_spacing);
    return Rect(x, y, m_itemSize.width, m_itemSize.height);
}

void GridCollectionLayout::itemRange(Rect visible, int itemCount, Size viewport, int &firstOut, int &lastOut) const {
    if (itemCount <= 0) {
        firstOut = 0;
        lastOut = -1;
        return;
    }
    const int columns = columnsFor(viewport);
    const float stride = m_itemSize.height + m_spacing;
    if (stride <= 0.f) {
        firstOut = 0;
        lastOut = itemCount - 1;
        return;
    }
    const int firstRow = std::max(0, static_cast<int>(std::floor((visible.origin.y - m_padding) / stride)));
    const int lastRow = static_cast<int>(std::floor((visible.origin.y + visible.size.height - m_padding) / stride));
    firstOut = firstRow * columns;
    lastOut = lastRow * columns + (columns - 1);
}

int GridCollectionLayout::nextIndex(int current, NavDirection direction, int itemCount, Size viewport) const {
    if (itemCount <= 0) { return -1; }
    const int columns = columnsFor(viewport);
    int next = current;
    switch (direction) {
        case NavDirection::Left: next = current - 1; break;        // linear: at a row edge — to the adjacent row
        case NavDirection::Right: next = current + 1; break;
        case NavDirection::Up: next = current - columns; break;    // same column, one row up
        case NavDirection::Down: next = current + columns; break;  // one row down (clamp at an incomplete row)
    }
    return std::clamp(next, 0, itemCount - 1);
}

VariableListCollectionLayout::VariableListCollectionLayout(MeasureFn measure, float spacing)
    : m_measure(std::move(measure))
    , m_spacing(spacing) {}

void VariableListCollectionLayout::invalidate() {
    m_cachedWidth = -1.f;
    m_cachedCount = -1;
}

void VariableListCollectionLayout::ensure(int itemCount, Size viewport) const {
    if (m_cachedCount == itemCount && m_cachedWidth == viewport.width) { return; }
    m_offsets.assign(static_cast<size_t>(itemCount) + 1, 0.f);
    float y = 0.f;
    for (int i = 0; i < itemCount; i++) {
        m_offsets[i] = y;
        y += (m_measure ? m_measure(i, viewport.width) : 0.f) + m_spacing;
    }
    m_offsets[itemCount] = y;
    m_cachedWidth = viewport.width;
    m_cachedCount = itemCount;
}

Size VariableListCollectionLayout::contentSize(int itemCount, Size viewport) const {
    ensure(itemCount, viewport);
    return Size(viewport.width, m_offsets.empty() ? 0.f : m_offsets.back());
}

Rect VariableListCollectionLayout::frameForItem(int index, Size viewport) const {
    if (index < 0 || m_cachedCount <= index) { ensure(std::max(m_cachedCount, index + 1), viewport); }
    if (index < 0 || static_cast<size_t>(index) + 1 >= m_offsets.size()) { return Rect(); }
    const float y = m_offsets[index];
    const float height = m_offsets[index + 1] - y - m_spacing;
    return Rect(0.f, y, viewport.width, height);
}

void VariableListCollectionLayout::itemRange(
    Rect visible, int itemCount, Size viewport, int &firstOut, int &lastOut
) const {
    ensure(itemCount, viewport);
    if (itemCount <= 0) {
        firstOut = 0;
        lastOut = -1;
        return;
    }
    const auto begin = m_offsets.begin();
    const auto end = begin + itemCount; // offsets[0..count-1] — row starts
    auto lower = std::upper_bound(begin, end, visible.origin.y);
    firstOut = static_cast<int>(std::max<std::ptrdiff_t>(0, lower - begin - 1));
    auto upper = std::lower_bound(begin, end, visible.origin.y + visible.size.height);
    lastOut = static_cast<int>(std::min<std::ptrdiff_t>(itemCount - 1, upper - begin));
}

int VariableListCollectionLayout::nextIndex(
    int current, NavDirection direction, int itemCount, Size viewport
) const {
    (void)viewport;
    int next = current;
    if (direction == NavDirection::Up || direction == NavDirection::Left) { next = current - 1; }
    if (direction == NavDirection::Down || direction == NavDirection::Right) { next = current + 1; }
    if (itemCount <= 0) { return 0; }
    return next < 0 ? 0 : (next >= itemCount ? itemCount - 1 : next);
}

} // namespace PandaUI
