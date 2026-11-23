#include "Sections/ScrollViewSection.hpp"

#include "Demo/DemoExample.hpp"
#include "Demo/DemoStyle.hpp"
#include "Demo/PageHeader.hpp"
#include "Demo/ThemedSurface.hpp"

#include <memory>
#include <string>

namespace PandaUI::Gallery {

ScrollViewSection::ScrollViewSection() {
    addSubviews();
    setupLayout();
    setupStyle();
}

void ScrollViewSection::addSubviews() {
    addSubview(std::make_shared<PageHeader>("ScrollView", "Scrolling with inertia and bounce; vertical and horizontal; sized to its content."));

    {
        auto example = std::make_shared<DemoExample>("Vertical", "ScrollDirection::Vertical, auto content size.");
        auto frame = std::make_shared<ThemedSurface>(SurfaceRole::SurfaceVariant, Style::CardRadius);
        frame->layout().setSize({360.f, 200.f});
        frame->layout().setFlexDirection(FlexDirection::Column);
        frame->setClipsToBounds(true);

        auto scroll = std::make_shared<ScrollView>();
        scroll->setScrollDirection(ScrollDirection::Vertical);
        scroll->setBackgroundColor(Color(0x00000000));
        scroll->layout().setFlexGrow(1.f);
        scroll->getContentView()->layout().setFlexDirection(FlexDirection::Column);
        scroll->getContentView()->layout().setGap(6.f);
        scroll->getContentView()->layout().setPadding(8.f);
        for (int i = 1; i <= 18; ++i) {
            auto row = std::make_shared<ThemedSurface>(SurfaceRole::Control, 6.f);
            row->layout().setHeight(Length::points(30.f));
            row->layout().setFlexDirection(FlexDirection::Row);
            row->layout().setAlignItems(Align::Center);
            row->layout().setPadding(Edge::Horizontal, 10.f);
            auto label = std::make_shared<Label>("Row " + std::to_string(i));
            label->setFont(Font(Style::BodyFont));
            row->addSubview(label);
            scroll->addContentSubview(row);
        }
        frame->addSubview(scroll);
        example->addContent(frame);
        addSubview(example);
    }
    {
        auto example = std::make_shared<DemoExample>(
            "Sized to Content", "setSizesToContent: as tall as its rows up to the max height of its frame, then it scrolls."
        );
        auto frame = std::make_shared<ThemedSurface>(SurfaceRole::SurfaceVariant, Style::CardRadius);
        frame->layout().setWidth(Length::points(360.f));
        frame->layout().setMaxHeight(Length::points(200.f));
        frame->layout().setFlexDirection(FlexDirection::Column);
        frame->setClipsToBounds(true);

        auto scroll = std::make_shared<ScrollView>();
        scroll->setSizesToContent(true);
        scroll->setBackgroundColor(Color(0x00000000));
        scroll->getContentView()->layout().setFlexDirection(FlexDirection::Column);
        scroll->getContentView()->layout().setGap(6.f);
        scroll->getContentView()->layout().setPadding(8.f);
        View *rows = scroll->getContentView().get();
        auto addRow = [rows] {
            auto row = std::make_shared<ThemedSurface>(SurfaceRole::Control, 6.f);
            row->layout().setHeight(Length::points(30.f));
            row->layout().setFlexDirection(FlexDirection::Row);
            row->layout().setAlignItems(Align::Center);
            row->layout().setPadding(Edge::Horizontal, 10.f);
            auto label = std::make_shared<Label>("Row " + std::to_string(rows->getSubviews().size() + 1));
            label->setFont(Font(Style::BodyFont));
            row->addSubview(label);
            rows->addSubview(row);
        };
        addRow();
        addRow();
        frame->addSubview(scroll);
        example->addContent(frame);

        auto buttons = std::make_shared<Panel>();
        buttons->setBackgroundColor(Color(0x00000000));
        buttons->layout().setFlexDirection(FlexDirection::Column);
        buttons->layout().setGap(6.f);
        buttons->layout().setAlignSelf(Align::Start); // stays at the top while the frame grows
        auto add = std::make_shared<Button>("Add Row");
        add->setOnClick([addRow](Button &) { addRow(); });
        buttons->addSubview(add);
        auto remove = std::make_shared<Button>("Remove Row");
        remove->setOnClick([rows](Button &) {
            if (rows->getSubviews().empty()) { return; }
            const std::shared_ptr<View> last = rows->getSubviews().back(); // alive while it leaves the list
            last->removeFromSuperview();
        });
        buttons->addSubview(remove);
        example->addContent(buttons);
        addSubview(example);
    }
    {
        auto example = std::make_shared<DemoExample>("Horizontal", "ScrollDirection::Horizontal.");
        auto frame = std::make_shared<ThemedSurface>(SurfaceRole::SurfaceVariant, Style::CardRadius);
        frame->layout().setSize({360.f, 84.f});
        frame->layout().setFlexDirection(FlexDirection::Column);
        frame->setClipsToBounds(true);

        auto scroll = std::make_shared<ScrollView>();
        scroll->setScrollDirection(ScrollDirection::Horizontal);
        scroll->setBackgroundColor(Color(0x00000000));
        scroll->layout().setFlexGrow(1.f);
        scroll->getContentView()->layout().setFlexDirection(FlexDirection::Row);
        scroll->getContentView()->layout().setGap(8.f);
        scroll->getContentView()->layout().setPadding(8.f);
        for (int i = 0; i < 16; ++i) {
            auto block = std::make_shared<Panel>();
            block->layout().setSize({60.f, 60.f});
            block->setBackgroundColor(ThemeColor::accent);
            block->surface().setCornerRadius(6.f);
            scroll->addContentSubview(block);
        }
        frame->addSubview(scroll);
        example->addContent(frame);
        addSubview(example);
    }
}

void ScrollViewSection::setupLayout() {
    layout().setFlexDirection(FlexDirection::Column);
    layout().setGap(Style::PageGap);
}

void ScrollViewSection::setupStyle() {
    setBackgroundColor(Color(0x00000000));
}

} // namespace PandaUI::Gallery
