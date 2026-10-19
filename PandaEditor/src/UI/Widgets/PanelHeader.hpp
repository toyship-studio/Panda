#pragma once

#include <PandaEditorKit/Widgets/EditorView.hpp>
#include <PandaEditorKit/Widgets/Icons.hpp>

#include <functional>
#include <memory>
#include <string>

namespace Editor {

// Editor panel header: no fill ("colorless"), the title is slightly larger than the body, with padding
// and a thin divider line at the bottom — so that the content does not run into the title. An optional
// accessory on the right (for example, the "Add" button of the hierarchy).
class PanelHeader final : public EditorView {
public:
    explicit PanelHeader(std::string title);

    void setAccessory(std::shared_ptr<PandaUI::View> accessory); // right accessory (a button etc.)
    void setCount(int count); // counter to the right of the title (number of entities); count < 0 — hide
    void setOnMaximize(std::function<void()> callback); // show the maximize button (zone zoom) on the right

private:
    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;

    std::string m_title;
    std::shared_ptr<PandaUI::Label> m_label;
    std::shared_ptr<PandaUI::Label> m_countLabel;
    std::shared_ptr<PandaUI::Panel> m_accessorySlot;
    std::shared_ptr<IconButton> m_maximizeBtn; // rightmost; hidden until a handler is set
    std::shared_ptr<PandaUI::Panel> m_divider;
};

} // namespace Editor
