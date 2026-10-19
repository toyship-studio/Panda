#include "UI/Sections/SegmentedControlSection.hpp"

#include "UI/Demo/DemoExample.hpp"
#include "UI/Demo/DemoStyle.hpp"
#include "UI/Demo/PageHeader.hpp"

#include <memory>

namespace Demo {

using namespace PandaUI;

SegmentedControlSection::SegmentedControlSection() {
    addSubviews();
    setupLayout();
    setupStyle();
}

void SegmentedControlSection::addSubviews() {
    addSubview(std::make_shared<PageHeader>(
        "SegmentedControl", "Exclusive choice among several segments — all options are in plain sight."
    ));

    {
        auto example = std::make_shared<DemoExample>("Value", "SegmentedControl::setOnSelectionChanged.");
        auto segmented = std::make_shared<SegmentedControl>();
        segmented->setSegments({"List", "Grid", "Columns"});
        m_stateLabel = std::make_shared<Label>("List");
        m_stateLabel->setFont(Font(Style::BodyFont));
        m_stateLabel->setTextRole(TextRole::Secondary);
        segmented->setOnSelectionChanged([this](SegmentedControl &sender) {
            if (m_stateLabel) { m_stateLabel->setText(sender.selectedText()); }
        });
        example->addContent(segmented);
        example->addContent(m_stateLabel);
        addSubview(example);
    }
    {
        auto example = std::make_shared<DemoExample>("Two segments", "Often used as a mode switch.");
        auto segmented = std::make_shared<SegmentedControl>();
        segmented->setSegments({"Local", "World"});
        segmented->setSelectedIndex(1);
        example->addContent(segmented);
        addSubview(example);
    }
    {
        auto example = std::make_shared<DemoExample>("Disabled", "setEnabled(false).");
        auto segmented = std::make_shared<SegmentedControl>();
        segmented->setSegments({"One", "Two", "Three"});
        segmented->setSelectedIndex(1);
        segmented->setEnabled(false);
        example->addContent(segmented);
        addSubview(example);
    }
}

void SegmentedControlSection::setupLayout() {
    layout().setFlexDirection(FlexDirection::Column);
    layout().setGap(Style::PageGap);
}

void SegmentedControlSection::setupStyle() {
    setBackgroundColor(Color(0x00000000));
}

} // namespace Demo
