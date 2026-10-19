#pragma once

#include "PandaUI/Core/Base.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace PandaUI {

// Drag payload. PandaUI does not interpret it:
//  type      — the application tag and the compatibility KEY ("treeview.nodes", "asset.texture",
//              "entity", "tabview.tab", ...): the drop target decides by it in canAcceptDrop whether
//              to accept the drop;
//  items     — ids of the dragged elements (multi-selection natively; single = one element).
//              The application interprets the ids by type (asset id, entity id, tab index/handle);
//  source    — the source widget: identity (reparent only within its own tree, moving tabs between
//              bars) and callbacks; the owner of the payload data is the source, it lives during
//              the drag;
//  ghostText — an optional caption of the "ghost" at the cursor during the drag.
struct DragPayload {
    std::string type;
    std::vector<std::uint64_t> items;
    void *source = nullptr;
    std::string ghostText;
};

// Information the drop target receives: what is being dragged and where the cursor is.
struct DragInfo {
    DragPayload payload;
    Point windowPosition;
    Point localPosition; // the cursor relative to the drop target's origin
};

} // namespace PandaUI
