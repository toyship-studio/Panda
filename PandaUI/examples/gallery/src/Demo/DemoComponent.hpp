#pragma once

#include <PandaUI/PandaUI.hpp>

namespace PandaUI::Gallery {

// Base for the custom Views of the demo. Convention: the constructor of a subclass calls
//   addSubviews(); setupLayout(); setupStyle();
// — uniform and readable. setupStyle() reads the colors from the current theme (Theme::current()),
// so on a theme change it is enough to call it again — themeChanged() does that.
class DemoComponent : public PandaUI::Panel {
public:
    DemoComponent() = default;

protected:
    virtual void addSubviews() {}
    virtual void setupLayout() {}
    virtual void setupStyle() {}

    void themeChanged() override {
        setupStyle();
    }
};

} // namespace PandaUI::Gallery
