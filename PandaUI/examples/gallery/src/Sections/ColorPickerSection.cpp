#include "Sections/ColorPickerSection.hpp"

#include "Demo/DemoExample.hpp"
#include "Demo/DemoStyle.hpp"
#include "Demo/PageHeader.hpp"

#include <cstdio>
#include <memory>

namespace PandaUI::Gallery {

ColorPickerSection::ColorPickerSection() {
    addSubviews();
    setupLayout();
    setupStyle();
}

void ColorPickerSection::addSubviews() {
    addSubview(std::make_shared<PageHeader>("Color", "Color swatches, an editable well and a full ColorPicker."));

    // 1. Swatches — a non-interactive ColorSwatch (display only).
    {
        auto example = std::make_shared<DemoExample>("Swatches", "ColorSwatch shows a color with a themed border.");
        const Color palette[] = {
            Color(0x2E7DFFFF), Color(0x34C759FF), Color(0xFFCC00FF),
            Color(0xFF3B30FF), Color(0xAF52DEFF), Color(0x5AC8FAFF),
        };
        for (Color color : palette) { example->addContent(std::make_shared<ColorSwatch>(color)); }
        addSubview(example);
    }

    // 2. Color well — a click opens the ColorPicker in an overlay; editing updates the swatch live.
    {
        auto example = std::make_shared<DemoExample>("Color well", "Click the swatch — a palette opens over the UI.");

        auto label = std::make_shared<Label>("#2E7DFF");
        label->setFont(Font(Style::BodyFont));
        label->setTextRole(TextRole::Secondary);
        Label *labelPtr = label.get();

        auto well = std::make_shared<ColorWell>(Color(0x2E7DFFFF));
        well->setOnColorChanged([labelPtr](ColorWell &sender) {
            Color color = sender.getColor();
            char buffer[8];
            std::snprintf(
                buffer, sizeof(buffer), "#%02X%02X%02X", static_cast<int>(color.r * 255.f + 0.5f),
                static_cast<int>(color.g * 255.f + 0.5f), static_cast<int>(color.b * 255.f + 0.5f)
            );
            labelPtr->setText(buffer);
        });

        example->addContent(well);
        example->addContent(label);
        addSubview(example);
    }

    // 3. A full ColorPicker: SV palette, hue strip, HEX input and preview.
    {
        auto example = std::make_shared<DemoExample>("ColorPicker", "Drag on the palette/hue, or type HEX (#RRGGBB).");

        auto picker = std::make_shared<ColorPicker>();
        picker->setColor(Color(0x2E7DFFFF));

        m_preview = std::make_shared<Panel>();
        m_preview->layout().setSize({72.f, 72.f});
        m_preview->setBackgroundColor(picker->getColor());
        m_preview->surface().setCornerRadius(10.f);
        m_preview->surface().setBorderWidth(1.f);

        picker->setOnColorChanged([preview = m_preview.get()](ColorPicker &sender) {
            preview->setBackgroundColor(sender.getColor());
        });

        example->addContent(picker);
        example->addContent(m_preview);
        addSubview(example);
    }
}

void ColorPickerSection::setupLayout() {
    layout().setFlexDirection(FlexDirection::Column);
    layout().setGap(Style::PageGap);
}

void ColorPickerSection::setupStyle() {
    setBackgroundColor(Color(0x00000000));
    if (m_preview) { m_preview->surface().setBorderColor(ThemeColor::border); }
}

} // namespace PandaUI::Gallery
