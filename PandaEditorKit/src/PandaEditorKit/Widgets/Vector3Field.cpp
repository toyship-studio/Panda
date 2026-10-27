#include "PandaEditorKit/Widgets/Vector3Field.hpp"

#include <PandaUI/Core/Theme.hpp>
#include <PandaUI/Elements/Control.hpp>
#include <PandaUI/Elements/Label.hpp>
#include <PandaUI/Elements/NumberField.hpp>
#include <PandaUI/Elements/TextField.hpp>

#include <algorithm>
#include <functional>
#include <string>
#include <utility>

using namespace PandaUI;

namespace Editor {

namespace {
    constexpr float AXIS_LABEL_LEFT = 6.f;  // offset of the colored axis letter from the left edge of the field
    constexpr float AXIS_LABEL_WIDTH = 14.f;
    constexpr float AXIS_LABEL_GAP = 4.f;
    // The plate of the axis button: from the left edge of the field to where the value starts.
    constexpr float AXIS_PLATE_WIDTH = AXIS_LABEL_LEFT + AXIS_LABEL_WIDTH + AXIS_LABEL_GAP;
    constexpr float AXIS_PLATE_ALPHA = 0.12f;
    constexpr float AXIS_PLATE_ALPHA_HOVERED = 0.20f;
    constexpr float AXIS_PLATE_ALPHA_PRESSED = 0.28f;
    constexpr float COMPONENT_GAP = 6.f;    // between X / Y / Z
    constexpr float FIELD_MIN_WIDTH = 40.f;
    constexpr float FIELD_FONT_SIZE = 14.f; // mono value (design axis-value 14px)
    // The value starts to the right of the clickable axis letter.
    const EdgeInsets FIELD_INSETS = EdgeInsets(5.f, AXIS_LABEL_LEFT + AXIS_LABEL_WIDTH + AXIS_LABEL_GAP, 6.f, 5.f);
    const char *AXIS_LETTERS[3] = {"X", "Y", "Z"};

    Color withBrightness(Color c, float factor) {
        return Color(
            std::clamp(c.r * factor, 0.f, 1.f),
            std::clamp(c.g * factor, 0.f, 1.f),
            std::clamp(c.b * factor, 0.f, 1.f),
            c.a
        );
    }

    // Colored axis button inside the field: an overlay on the left, absolutely positioned on top of
    // NumberField, a tinted plate in the color of the axis with the letter on it. The plate runs from
    // the very left edge: the field clips its children by its rounded shape, so the plate takes the
    // corners of the field. A click resets the component to 0.
    class AxisLabel final : public Control {
    public:
        AxisLabel(const std::string &letter, Color color)
            : m_color(color)
            , m_label(std::make_shared<Label>(letter)) {
            setCursor(Cursor::PointingHand);
            setFocusable(false); // a click target for reset, not a tab stop; does not steal focus from the field
            layout().setPositionType(PositionType::Absolute);
            layout().setPosition(Edge::Left, Length::points(0.f));
            layout().setPosition(Edge::Top, Length::points(0.f));
            layout().setPosition(Edge::Bottom, Length::points(0.f));
            layout().setWidth(Length::points(AXIS_PLATE_WIDTH));
            layout().setJustifyContent(Justify::Center);
            layout().setAlignItems(Align::Center);

            // Monospaced colored axis letter: looks like a prefix action inside an input.
            m_label->setFont(Font(12.f));
            m_label->setFont(m_label->getFont().withFamily(FontFamily::Mono));
            m_label->setFont(m_label->getFont().withWeight(FontWeight::Bold));
            m_label->setTextAlignment(TextAlignment::Center);
            addSubview(m_label);

            applyStyle();
        }

        void setOnReset(std::function<void()> callback) {
            m_onReset = std::move(callback);
        }

    private:
        void performPrimaryAction() override {
            if (m_onReset) { m_onReset(); }
        }
        void controlStateChanged(ControlState, ControlState) override {
            applyStyle();
        }
        void applyStyle() {
            float factor = 1.f;
            float alpha = AXIS_PLATE_ALPHA;
            if (hasControlState(ControlState::Highlighted)) {
                factor = 0.8f;
                alpha = AXIS_PLATE_ALPHA_PRESSED;
            } else if (hasControlState(ControlState::Hovered)) {
                factor = 1.2f;
                alpha = AXIS_PLATE_ALPHA_HOVERED;
            }
            setBackgroundColor(ThemeColor(m_color).withAlpha(alpha)); // a translucent tint: the field border shows through
            m_label->setTextColor(withBrightness(m_color, factor));
        }

        Color m_color;
        std::shared_ptr<Label> m_label;
        std::function<void()> m_onReset;
    };

} // namespace

Vector3Field::Vector3Field()
    : m_onValueChanged(nullptr) {
    setBackgroundColor(Color(0x00000000));
    layout().setFlexDirection(FlexDirection::Row);
    layout().setAlignItems(Align::Stretch);
    layout().setGap(COMPONENT_GAP);

    // Theme axis colors: X=danger, Y=success, Z=light blue (accentHover) — exactly per design.md §1.
    const Color axisColors[3] = {Theme::current().danger, Theme::current().success, Theme::current().accentHover};

    for (int i = 0; i < 3; ++i) {
        auto field = std::make_shared<NumberField>();
        field->setFont(Font(FIELD_FONT_SIZE));
        field->setFont(field->getFont().withFamily(FontFamily::Mono));          // mono value (numbers are aligned)
        field->setContentInsets(FIELD_INSETS);           // room on the left for the colored axis letter
        field->setTextAlignment(TextAlignment::Center);  // the number is centered in the field
        field->setDecimals(2);                           // Transform: strictly 2 digits after the point
        field->layout().setFlexGrow(1.f);      // the three fields split the width equally
        field->layout().setFlexShrink(1.f);
        field->layout().setMinWidth(Length::points(FIELD_MIN_WIDTH));
        // The axis plate is a child of the field panel: the panel clips by the rounded shape of the
        // inner TextField (the same radius), so the plate ends at the field's corners.
        field->surface().setCornerRadii(TextField::defaultAppearance().cornerRadius);
        field->setClipsToBounds(true);
        field->setOnValueChanged([this](NumberField &) {
            if (m_onValueChanged) { m_onValueChanged(*this); }
        });

        // The axis letter is an overlay inside the field (on top of the inner TextField).
        auto axis = std::make_shared<AxisLabel>(AXIS_LETTERS[i], axisColors[i]);
        axis->setOnReset([this, i]() {
            m_fields[i]->setValue(0.f);
            if (m_onValueChanged) { m_onValueChanged(*this); }
        });
        field->addSubview(axis);

        m_fields[i] = field;
        addSubview(field);
    }
}

void Vector3Field::setValue(float x, float y, float z) {
    m_fields[0]->setValue(x);
    m_fields[1]->setValue(y);
    m_fields[2]->setValue(z);
}

void Vector3Field::setMixed(bool x, bool y, bool z) {
    m_fields[0]->setMixed(x);
    m_fields[1]->setMixed(y);
    m_fields[2]->setMixed(z);
}

float Vector3Field::x() const {
    return m_fields[0]->getValue();
}

float Vector3Field::y() const {
    return m_fields[1]->getValue();
}

float Vector3Field::z() const {
    return m_fields[2]->getValue();
}

void Vector3Field::setOnValueChanged(ValueChangedCallback callback) {
    m_onValueChanged = std::move(callback);
}

} // namespace Editor
