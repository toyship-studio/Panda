#pragma once

#include "PandaUI/Core/Base.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace PandaUI {

class Context;
class View;

// An element of a window as a UI test sees it (Core/Accessibility.hpp): a view that says what it
// is or was given an id. The tree is a snapshot of one moment: it is taken, read and thrown away
// before the UI is updated again — `view` is not kept.
struct AccessibilityElement {
    View *view = nullptr;
    std::string role; // what the view says it is; "view" — a container that was only given an id
    std::string id;
    std::string label;
    std::string value;
    Rect frame;   // window coordinates
    Rect visible; // the part of the frame a press can reach: what the frames of the views above leave of it
    bool enabled = true; // false — the view or one above it is disabled
    bool selected = false;
    bool focused = false;
    std::vector<AccessibilityElement> children;

    bool inSight() const {
        return visible.size.width > 0.f && visible.size.height > 0.f;
    }
    // What the element goes by: its id, else its label, else its role.
    const std::string &name() const {
        return !id.empty() ? id : !label.empty() ? label : role;
    }
};

// The elements of everything the context shows, the lowest layer first (Context::layers): the
// root view, then the overlays — popups, menus, modals — then the tooltip. A hidden view and what
// is inside it are not elements. A view that is no element (a plain container) is not in the
// tree: the elements inside it stand in its place.
std::vector<AccessibilityElement> accessibilityTree(Context &context);

// What a path names in a tree of elements.
//
// A path is segments separated by "/". The first segment names an element anywhere in the
// window; each next one — an element inside the one named before it, at any depth:
// "inspector/Transform/Position/X". A segment is the id of an element; where no element there
// has such an id — a label; where there is no such label either — a role ("menu", "dialog").
// An element inside another one that the same segment names is the same thing said twice (the
// title of a row inside the row): only the outer one counts. "role:name" is an element of that
// role with that id or label — "tab:Player" where a row is named Player too. "name[2]" is the
// third of the elements the segment names, in the order of the tree.
struct AccessibilityMatch {
    std::vector<const AccessibilityElement *> elements; // what the whole path names; empty — nothing
    // When the path names nothing: how many of its segments do, and what they name — where the
    // next segment was looked for (0 and empty — the whole window).
    size_t resolved = 0;
    std::vector<const AccessibilityElement *> scope;
    std::vector<std::string> segments; // the names of the segments, without the index
};
AccessibilityMatch findAccessibilityElements(const std::vector<AccessibilityElement> &tree, std::string_view path);

} // namespace PandaUI
