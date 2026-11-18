#include "PandaUI/Runtime/AccessibilityTree.hpp"

#include "PandaUI/Core/Accessibility.hpp"
#include "PandaUI/Core/View.hpp"
#include "PandaUI/Runtime/Context.hpp"

#include <algorithm>
#include <cstdlib>

namespace PandaUI {

namespace {

    Rect intersection(const Rect &a, const Rect &b) {
        const float left = std::max(a.origin.x, b.origin.x);
        const float top = std::max(a.origin.y, b.origin.y);
        const float right = std::min(a.origin.x + a.size.width, b.origin.x + b.size.width);
        const float bottom = std::min(a.origin.y + a.size.height, b.origin.y + b.size.height);
        if (right <= left || bottom <= top) { return Rect(Point(left, top), Size(0.f, 0.f)); }
        return Rect(Point(left, top), Size(right - left, bottom - top));
    }

    // The view of the element the keyboard focus is in: the innermost element around the view
    // that has it — a number field for the text field inside it.
    const View *elementAround(View *focused) {
        std::vector<View *> chain;
        for (View *view = focused; view != nullptr; view = view->getSuperview()) { chain.push_back(view); }
        const View *element = nullptr;
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) { // the outermost first
            Accessibility described;
            (*it)->describeAccessibility(described);
            if (described.role == nullptr && (*it)->getAccessibilityId().empty()) { continue; }
            element = *it;
            if (described.atomic) { break; } // what is inside it is no element
        }
        return element;
    }

    // `reach` is what the views above leave for a press: a view outside the frame of its parent is
    // not hit (View::hitTest), whatever the parent draws. `focus` — the view of the element the
    // keyboard focus is in.
    void collect(View &view, const Rect &reach, bool enabled, const View *focus, std::vector<AccessibilityElement> &out) {
        if (view.isHidden() || view.getOpacity() <= 0.f) { return; }
        Accessibility described;
        view.describeAccessibility(described);
        const Rect frame = view.getWorldFrame();
        const Rect visible = intersection(frame, reach);
        const bool viewEnabled = enabled && view.isEnabled();
        const std::string &id = view.getAccessibilityId();

        std::vector<AccessibilityElement> *children = &out;
        if (described.role != nullptr || !id.empty()) {
            AccessibilityElement &element = out.emplace_back();
            element.view = &view;
            element.role = described.role != nullptr ? described.role : "view";
            element.id = id;
            element.label = std::move(described.label);
            element.value = std::move(described.value);
            element.frame = frame;
            element.visible = visible;
            element.enabled = viewEnabled;
            element.selected = described.selected;
            element.focused = &view == focus;
            if (described.atomic) { return; }
            children = &element.children;
        }
        // `element` stays where it is: while its children are collected nothing is appended to `out`.
        for (const std::shared_ptr<View> &subview : view.getSubviews()) {
            collect(*subview, visible, viewEnabled, focus, *children);
        }
    }

    struct Segment {
        std::string name;
        int index = -1; // "name[2]": the third of what the name names; -1 — all of it
    };

    std::vector<Segment> parsePath(std::string_view path) {
        std::vector<Segment> segments;
        size_t start = 0;
        while (start <= path.size()) {
            size_t end = path.find('/', start);
            if (end == std::string_view::npos) { end = path.size(); }
            std::string_view part = path.substr(start, end - start);
            start = end + 1;
            if (part.empty()) { continue; } // "a//b", a leading or a trailing slash
            Segment segment;
            const size_t open = part.rfind('[');
            if (part.back() == ']' && open != std::string_view::npos && open > 0) {
                const std::string_view digits = part.substr(open + 1, part.size() - open - 2);
                const bool number = !digits.empty() && std::all_of(digits.begin(), digits.end(), [](char c) {
                    return c >= '0' && c <= '9';
                });
                if (number) {
                    segment.index = std::atoi(std::string(digits).c_str());
                    part = part.substr(0, open);
                }
            }
            segment.name = std::string(part);
            segments.push_back(std::move(segment));
        }
        return segments;
    }

    enum class Tier { Id, Label, Role };

    // `role` — the element also has to be of this role; empty — of any.
    bool names(const AccessibilityElement &element, const std::string &name, const std::string &role, Tier tier) {
        if (!role.empty() && element.role != role) { return false; }
        switch (tier) {
            case Tier::Id: return element.id == name;
            case Tier::Label: return element.label == name;
            case Tier::Role: return element.role == name;
        }
        return false;
    }

    // The outermost elements among `elements` and inside them that the name names.
    void match(
        const std::vector<AccessibilityElement> &elements, const std::string &name, const std::string &role, Tier tier,
        std::vector<const AccessibilityElement *> &found
    ) {
        for (const AccessibilityElement &element : elements) {
            if (names(element, name, role, tier)) {
                found.push_back(&element);
            } else {
                match(element.children, name, role, tier, found);
            }
        }
    }

    // What a segment names in a scope (`tree` when the scope is the whole window): an id, else a
    // label, else a role; and when it is none of them and reads "role:name" — an element of
    // that role with that id, else with that label.
    std::vector<const AccessibilityElement *> matchSegment(
        const std::vector<AccessibilityElement> &tree, const std::vector<const AccessibilityElement *> &scope, bool wholeWindow,
        const std::string &name
    ) {
        std::vector<const AccessibilityElement *> found;
        auto look = [&](const std::string &what, const std::string &role, Tier tier) {
            if (wholeWindow) {
                match(tree, what, role, tier, found);
            } else {
                for (const AccessibilityElement *element : scope) { match(element->children, what, role, tier, found); }
            }
            return !found.empty();
        };
        for (Tier tier : {Tier::Id, Tier::Label, Tier::Role}) {
            if (look(name, std::string(), tier)) { return found; }
        }
        const size_t colon = name.find(':');
        if (colon != std::string::npos && colon > 0 && colon + 1 < name.size()) {
            const std::string role = name.substr(0, colon);
            const std::string rest = name.substr(colon + 1);
            for (Tier tier : {Tier::Id, Tier::Label}) {
                if (look(rest, role, tier)) { return found; }
            }
        }
        return found;
    }

} // namespace

std::vector<AccessibilityElement> accessibilityTree(Context &context) {
    context.layoutIfNeeded();
    std::vector<AccessibilityElement> tree;
    const Rect window(Point(0.f, 0.f), context.getViewportSize());
    const View *focus = elementAround(context.getFocusedView());
    for (View *layer : context.layers()) { collect(*layer, window, true, focus, tree); }
    return tree;
}

AccessibilityMatch findAccessibilityElements(const std::vector<AccessibilityElement> &tree, std::string_view path) {
    AccessibilityMatch result;
    const std::vector<Segment> segments = parsePath(path);
    for (const Segment &segment : segments) { result.segments.push_back(segment.name); }
    if (segments.empty()) { return result; }

    std::vector<const AccessibilityElement *> scope; // empty before the first segment: the whole window
    for (size_t i = 0; i < segments.size(); ++i) {
        std::vector<const AccessibilityElement *> found = matchSegment(tree, scope, i == 0, segments[i].name);
        if (segments[i].index >= 0) {
            if (static_cast<size_t>(segments[i].index) < found.size()) {
                found = {found[static_cast<size_t>(segments[i].index)]};
            } else {
                found.clear();
            }
        }
        if (found.empty()) {
            result.resolved = i;
            result.scope = std::move(scope);
            return result;
        }
        scope = std::move(found);
    }
    result.resolved = segments.size();
    result.elements = std::move(scope);
    return result;
}

} // namespace PandaUI
