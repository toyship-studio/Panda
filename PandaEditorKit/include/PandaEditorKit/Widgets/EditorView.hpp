#pragma once

#include <PandaUI/PandaUI.hpp>

namespace Editor {

// Base for the editor-specific views of the editor and of the editor parts of modules.
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
