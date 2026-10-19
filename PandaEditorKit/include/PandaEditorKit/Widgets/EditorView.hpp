#pragma once

#include <PandaUI/PandaUI.hpp>

namespace Editor {

// Base for editor-specific views in PandaUITest.
class EditorView : public PandaUI::Panel {
public:
    EditorView() = default;

protected:
    virtual void addSubviews() {}
    virtual void setupLayout() {}
    virtual void setupStyle() {}

    void themeChanged() override {
        setupStyle();
    }
};

} // namespace Editor
