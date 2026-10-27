#include "Sections/ProgressBarSection.hpp"

#include "Demo/DemoExample.hpp"
#include "Demo/DemoStyle.hpp"
#include "Demo/PageHeader.hpp"

#include <cmath>
#include <memory>
#include <string>

namespace PandaUI::Gallery {

namespace {
    // A fixed-width container: ProgressBar stretches to 100% and is visible at full length.
    std::shared_ptr<Panel> makeColumn(float width) {
        auto column = std::make_shared<Panel>();
        column->setBackgroundColor(Color(0x00000000));
        column->layout().setFlexDirection(FlexDirection::Column);
        column->layout().setGap(10.f);
        column->layout().setWidth(Length::points(width));
        return column;
    }
}

ProgressBarSection::ProgressBarSection() {
    addSubviews();
    setupLayout();
    setupStyle();
}

void ProgressBarSection::addSubviews() {
    addSubview(std::make_shared<PageHeader>("ProgressBar", "Progress indicator: determinate (0..1) and indeterminate."));

    {
        auto example = std::make_shared<DemoExample>("Determinate progress", "setProgress(0..1), here via the slider.");
        auto column = makeColumn(320.f);

        auto row = std::make_shared<Panel>();
        row->setBackgroundColor(Color(0x00000000));
        row->layout().setFlexDirection(FlexDirection::Row);
        row->layout().setAlignItems(Align::Center);
        row->layout().setGap(12.f);

        auto slider = std::make_shared<Slider>();
        slider->setRange(0.f, 1.f);
        slider->setValue(0.35f);
        slider->layout().setFlexGrow(1.f);

        m_valueLabel = std::make_shared<Label>("35%");
        m_valueLabel->setFont(Font(Style::BodyFont));
        m_valueLabel->setTextRole(TextRole::Secondary);
        m_valueLabel->layout().setMinWidth(Length::points(40.f));

        row->addSubview(slider);
        row->addSubview(m_valueLabel);

        m_bar = std::make_shared<ProgressBar>();
        m_bar->setProgress(0.35f);
        m_bar->layout().setWidth(Length::percent(100.f));

        slider->setOnValueChanged([this](Slider &sender) {
            if (m_bar) { m_bar->setProgress(sender.getValue()); }
            if (m_valueLabel) {
                const int percent = static_cast<int>(std::lround(sender.getValue() * 100.f));
                m_valueLabel->setText(std::to_string(percent) + "%");
            }
        });

        column->addSubview(row);
        column->addSubview(m_bar);
        example->addContent(column);
        addSubview(example);
    }
    {
        auto example = std::make_shared<DemoExample>("Indeterminate", "setIndeterminate(true) — a running segment.");
        auto column = makeColumn(320.f);
        auto bar = std::make_shared<ProgressBar>();
        bar->setIndeterminate(true);
        bar->layout().setWidth(Length::percent(100.f));
        column->addSubview(bar);
        example->addContent(column);
        addSubview(example);
    }
}

void ProgressBarSection::setupLayout() {
    layout().setFlexDirection(FlexDirection::Column);
    layout().setGap(Style::PageGap);
}

void ProgressBarSection::setupStyle() {
    setBackgroundColor(Color(0x00000000));
}

} // namespace PandaUI::Gallery
