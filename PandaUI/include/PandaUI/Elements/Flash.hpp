#pragma once

#include <cmath>

namespace PandaUI {

// The blink of a row or a cell that answers "here it is" (TreeView::flashItem,
// CollectionView::flashIndex): three short flashes in the selection color, the selection itself
// is not touched.
namespace Flash {

    inline constexpr double PERIOD = 0.3;    // one flash: lit, then dark
    inline constexpr double LIT = 0.18;      // the lit part of a period
    inline constexpr double DURATION = 0.9;  // three periods

    inline bool isLit(double elapsed) {
        return std::fmod(elapsed, PERIOD) < LIT;
    }

} // namespace Flash

} // namespace PandaUI
