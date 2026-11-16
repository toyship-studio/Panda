#pragma once

#include <string>

namespace PandaUI {

// What a view is to someone who does not look at the picture: a UI test, a tool that uses the
// application in place of the user. The four things UIKit gives a UI test — an identifier, a
// label, a value, the traits — in the same split: the id is given to a view by the code that
// builds the screen (View::setAccessibilityId), the rest the view says about itself
// (View::describeAccessibility).
//
// A view without a role and without an id is a plain container: it is not an element, its
// subviews stand in its place in the tree of elements (Runtime/AccessibilityTree.hpp).
struct Accessibility {
    // What kind of thing the view is: "button", "label", "textField", "toggle", "property",
    // "section", "tab", "menuItem", "row", "cell", … A literal of the class of the view;
    // nullptr — a plain container.
    const char *role = nullptr;
    std::string label; // the text it shows, the name it goes by: the title of a button, of a row, of a tab
    std::string value; // the state it holds: the text of a field, "on" / "off", a number, "expanded"
    bool selected = false; // the chosen one of its kind: a tab, a row of a list, a segment
    // The subviews are parts of this element (the title of a button), not elements of their own.
    bool atomic = false;
};

} // namespace PandaUI
