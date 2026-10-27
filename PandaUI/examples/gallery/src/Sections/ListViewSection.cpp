#include "Sections/ListViewSection.hpp"

#include "Demo/DemoExample.hpp"
#include "Demo/DemoStyle.hpp"
#include "Demo/PageHeader.hpp"

#include <memory>
#include <string>

namespace PandaUI::Gallery {

namespace {
    const char *kItems[] = {"Apple",  "Banana",   "Cherry", "Date", "Elderberry",
                            "Fig",    "Grape",    "Kiwi",   "Lemon", "Mango",
                            "Orange", "Pear"};

    void configureBox(View &view) {
        view.layout().setWidth(Length::points(360.f));
        view.layout().setHeight(Length::points(200.f));
        view.surface().setCornerRadius(Radius::md);
        view.surface().setBorderWidth(1.f);
    }
}

ListViewSection::ListViewSection() {
    addSubviews();
    setupLayout();
    setupStyle();
}

void ListViewSection::addSubviews() {
    addSubview(std::make_shared<PageHeader>("List", "A scrollable list of selectable rows."));

    {
        auto example = std::make_shared<DemoExample>("Single selection", "Click selects one row; the selection moves.");

        m_single = std::make_shared<ListView>();
        configureBox(*m_single);
        for (const char *item : kItems) { m_single->addItem(item); }

        m_singleStatus = std::make_shared<Label>("Selected: —");
        m_singleStatus->setFont(Font(Style::BodyFont));
        m_singleStatus->setTextRole(TextRole::Secondary);

        Label *status = m_singleStatus.get();
        ListView *list = m_single.get();
        m_single->setOnSelectionChanged([status, list](ListView &) {
            const int index = list->selectedIndex();
            status->setText(index < 0 ? "Selected: —" : "Selected: " + std::string(kItems[index]));
        });

        auto column = std::make_shared<Panel>();
        column->setBackgroundColor(Color(0x00000000));
        column->layout().setFlexDirection(FlexDirection::Column);
        column->layout().setGap(8.f);
        column->addSubview(m_single);
        column->addSubview(m_singleStatus);
        example->addContent(column);
        addSubview(example);
    }

    {
        auto example = std::make_shared<DemoExample>("Multiple selection", "Click — one row, ⌘/Ctrl-click — add.");

        m_multi = std::make_shared<ListView>();
        m_multi->setSelectionMode(ListSelectionMode::Multiple);
        configureBox(*m_multi);
        for (const char *item : kItems) { m_multi->addItem(item); }

        m_multiStatus = std::make_shared<Label>("Selected: 0");
        m_multiStatus->setFont(Font(Style::BodyFont));
        m_multiStatus->setTextRole(TextRole::Secondary);

        Label *status = m_multiStatus.get();
        ListView *list = m_multi.get();
        m_multi->setOnSelectionChanged([status, list](ListView &) {
            status->setText("Selected: " + std::to_string(list->selectedIndices().size()));
        });

        auto column = std::make_shared<Panel>();
        column->setBackgroundColor(Color(0x00000000));
        column->layout().setFlexDirection(FlexDirection::Column);
        column->layout().setGap(8.f);
        column->addSubview(m_multi);
        column->addSubview(m_multiStatus);
        example->addContent(column);
        addSubview(example);
    }
}

void ListViewSection::setupLayout() {
    layout().setFlexDirection(FlexDirection::Column);
    layout().setGap(Style::PageGap);
}

void ListViewSection::setupStyle() {
    setBackgroundColor(Color(0x00000000));
    const Theme &t = Theme::current();
    for (ListView *list : {m_single.get(), m_multi.get()}) {
        if (!list) { continue; }
        list->setBackgroundColor(t.surfaceVariant);
        list->surface().setBorderColor(t.border);
    }
}

} // namespace PandaUI::Gallery
