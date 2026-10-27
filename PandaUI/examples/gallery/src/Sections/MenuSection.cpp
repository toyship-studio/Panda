#include "Sections/MenuSection.hpp"

#include "Demo/DemoExample.hpp"
#include "Demo/DemoStyle.hpp"
#include "Demo/PageHeader.hpp"
#include "Sections/ContextMenuArea.hpp"

#include <memory>
#include <string>

namespace PandaUI::Gallery {

MenuSection::MenuSection() {
    addSubviews();
    setupLayout();
    setupStyle();
}

void MenuSection::addSubviews() {
    addSubview(std::make_shared<PageHeader>("Menu", "A dropdown menu overlaying the UI; closes on a click outside."));

    {
        auto example = std::make_shared<DemoExample>("Dropdown", "MenuButton with items and a separator.");

        m_status = std::make_shared<Label>("Nothing selected");
        m_status->setFont(Font(Style::BodyFont));
        m_status->setTextRole(TextRole::Secondary);
        Label *status = m_status.get();

        auto button = std::make_shared<MenuButton>("Options");
        button->addItem("New", [status] { status->setText("Selected: New"); });
        button->addItem("Open", [status] { status->setText("Selected: Open"); });
        button->addItem("Save", [status] { status->setText("Selected: Save"); });
        button->addSeparator();
        button->addItem("Settings", [status] { status->setText("Selected: Settings"); });

        example->addContent(button);
        example->addContent(m_status);
        addSubview(example);
    }
    {
        auto example = std::make_shared<DemoExample>(
            "Context menu",
            "A right click on the area shows a menu at the cursor position. The Insert item is a submenu "
            "(hover or →/Enter); nesting is unlimited."
        );
        example->addContent(std::make_shared<ContextMenuArea>());
        addSubview(example);
    }
    {
        addSubview(std::make_shared<DemoExample>(
            "Behavior",
            "The menu is drawn on top of the whole interface (an overlay in Context) and closes on a click outside "
            "or after an item is selected. When shown, the menu takes the keyboard: ↑/↓ — move between items, "
            "Enter — select, →/← — submenu, Escape — close; Tab is trapped inside the popup, "
            "and after closing, the focus returns to its previous owner."
        ));
    }
}

void MenuSection::setupLayout() {
    layout().setFlexDirection(FlexDirection::Column);
    layout().setGap(Style::PageGap);
}

void MenuSection::setupStyle() {
    setBackgroundColor(Color(0x00000000));
}

} // namespace PandaUI::Gallery
