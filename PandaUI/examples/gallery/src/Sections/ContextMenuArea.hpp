#pragma once

#include "Demo/DemoComponent.hpp"

#include <memory>

namespace PandaUI::Gallery {

// An area that shows a context menu on right click (an overlay at the cursor position).
// It demonstrates that Popup+Menu also work without an anchor button — from an arbitrary point.
class ContextMenuArea final : public DemoComponent {
public:
    ContextMenuArea();

private:
    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;

    bool pointerDown(PandaUI::PointerEvent &event) override;
    void showMenu(PandaUI::Point position);

    std::shared_ptr<PandaUI::Label> m_hint;
};

} // namespace PandaUI::Gallery
