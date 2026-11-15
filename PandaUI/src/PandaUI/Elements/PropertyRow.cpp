#include "PandaUI/Elements/PropertyRow.hpp"

#include "PandaUI/Elements/Label.hpp"
#include "PandaUI/Elements/NumberField.hpp"
#include "PandaUI/Elements/Toggle.hpp"

#include <utility>

namespace PandaUI {

PropertyRowAppearance &PropertyRow::defaultAppearance() {
    static PropertyRowAppearance appearance;
    return appearance;
}

PropertyRow::PropertyRow(std::string title)
    : m_appearance(defaultAppearance()) {
    setBackgroundColor(Color(0x00000000));
    layout().setFlexDirection(FlexDirection::Row);
    layout().setAlignItems(Align::Center);

    m_labelColumn = std::make_shared<Panel>();
    m_labelColumn->setBackgroundColor(Color(0x00000000));
    m_labelColumn->layout().setFlexDirection(FlexDirection::Column);
    m_labelColumn->layout().setJustifyContent(Justify::Center);
    addSubview(m_labelColumn);

    m_title = std::move(title);
    m_label = std::make_shared<Label>(m_title);
    m_label->setNumberOfLines(0); // a long title wraps instead of being cut; the row grows with it
    m_labelColumn->addSubview(m_label);
    applyAppearance();
}

void PropertyRow::setAppearance(const PropertyRowAppearance &appearance) {
    m_appearance = appearance;
    applyAppearance();
}

void PropertyRow::applyAppearance() {
    layout().setGap(m_appearance.gap);
    layout().setMinHeight(Length::points(m_appearance.minHeight));
    m_label->setFont(m_appearance.labelFont);
    // Over a description the title is the name of a thing, not the label of a field.
    m_label->setTextColor(m_description ? m_appearance.describedLabelColor : m_appearance.labelColor);
    m_labelColumn->layout().setGap(m_appearance.descriptionGap);
    if (m_description) {
        m_description->setFont(m_appearance.descriptionFont);
        m_description->setTextColor(m_appearance.descriptionColor);
    }
    layout().setPadding(Edge::Vertical, m_description ? m_appearance.describedPadding : 0.f);
    applyPlacement();
}

void PropertyRow::applyPlacement() {
    Layout &label = m_labelColumn->layout();
    if (m_placement == PropertyRowPlacement::Trailing) {
        // The label takes what the editor leaves: nothing is cut to the width of a column.
        label.setWidth(Length::autoValue());
        label.setMinWidth(Length::autoValue());
        label.setMaxWidth(Length::autoValue());
        label.setFlexBasis(0.f);
        label.setFlexGrow(1.f);
        label.setFlexShrink(1.f);
    } else {
        if (m_appearance.labelShare > 0.f) {
            label.setWidth(Length::percent(m_appearance.labelShare * 100.f));
            label.setMinWidth(Length::points(m_appearance.labelMinWidth));
            label.setMaxWidth(Length::points(m_appearance.labelMaxWidth));
        } else {
            label.setWidth(Length::points(m_appearance.labelMinWidth));
        }
        label.setFlexBasisAuto();
        label.setFlexGrow(0.f);
        label.setFlexShrink(0.f); // the label keeps the column width
    }
    if (!m_editor) { return; }
    const bool fill = m_placement == PropertyRowPlacement::Fill;
    m_editor->layout().setFlexGrow(fill ? 1.f : 0.f);
    m_editor->layout().setFlexShrink(fill ? 1.f : 0.f);
    setNeedsLayout();
}

PropertyRow::PropertyRow(std::string title, std::shared_ptr<View> editor, bool fill)
    : PropertyRow(std::move(title)) {
    setEditor(std::move(editor), fill);
}

PropertyRow::~PropertyRow() = default;

void PropertyRow::setTitle(std::string title) {
    m_title = std::move(title);
    refreshLabel();
}

void PropertyRow::setMixed(bool mixed) {
    if (m_mixed == mixed) { return; }
    m_mixed = mixed;
    refreshLabel();
}

void PropertyRow::refreshLabel() {
    m_label->setText(m_mixed ? "* " + m_title : m_title);
}

void PropertyRow::setLabelNumberOfLines(int numberOfLines) {
    m_label->setNumberOfLines(numberOfLines);
}

void PropertyRow::setLabelWidth(float width) {
    m_appearance.labelShare = 0.f;
    m_appearance.labelMinWidth = width;
    applyPlacement();
}

void PropertyRow::setDescription(std::string description) {
    if (description.empty()) {
        if (m_description) {
            m_description->removeFromSuperview();
            m_description.reset();
            applyAppearance();
        }
        return;
    }
    if (!m_description) {
        m_description = std::make_shared<Label>(description);
        m_description->setNumberOfLines(0);
        m_labelColumn->addSubview(m_description);
        applyAppearance();
    } else {
        m_description->setText(std::move(description));
    }
}

void PropertyRow::setHelp(std::string help) {
    m_labelColumn->setTooltip(std::move(help));
}

void PropertyRow::setVisible(bool visible) {
    setCollapsed(!visible);
}

void PropertyRow::setIndent(float indent) {
    m_labelColumn->layout().setPadding(Edge::Left, indent);
}

void PropertyRow::setEditor(std::shared_ptr<View> editor, bool fill) {
    if (m_editor) { m_editor->removeFromSuperview(); }
    m_editor = std::move(editor);
    m_scrubField = dynamic_cast<NumberField *>(m_editor.get());
    m_scrubbing = false;
    // The label of a number is its handle: the cursor says it can be dragged.
    m_labelColumn->setCursor(m_scrubField ? Cursor::ResizeEW : Cursor::Inherit);
    if (!m_editor) { return; }

    if (fill) {
        m_placement = PropertyRowPlacement::Fill;
    } else if (m_appearance.trailingToggles && dynamic_cast<Toggle *>(m_editor.get()) != nullptr) {
        m_placement = PropertyRowPlacement::Trailing;
    } else {
        m_placement = PropertyRowPlacement::Leading;
    }
    addSubview(m_editor);
    applyPlacement();
}

void PropertyRow::setPlacement(PropertyRowPlacement placement) {
    if (m_placement == placement) { return; }
    m_placement = placement;
    applyPlacement();
}

bool PropertyRow::overLabel(Point windowPosition) const {
    return m_labelColumn->pointInside(windowPosition);
}

bool PropertyRow::pointerDown(PointerEvent &event) {
    if (m_scrubField == nullptr || event.button != PointerButton::Left || !m_scrubField->isEnabled() ||
        !overLabel(event.position)) {
        return false;
    }
    m_scrubbing = true;
    m_scrubX = event.position.x;
    m_scrubField->beginScrub();
    return true;
}

bool PropertyRow::pointerMove(PointerEvent &event) {
    if (!m_scrubbing || m_scrubField == nullptr) { return false; }
    const float pixels = event.position.x - m_scrubX;
    m_scrubX = event.position.x;
    if (pixels != 0.f) { m_scrubField->scrub(pixels, event.isShiftDown, event.isAltDown); }
    return true;
}

bool PropertyRow::pointerUp(PointerEvent &) {
    if (!m_scrubbing) { return false; }
    m_scrubbing = false;
    if (m_scrubField != nullptr) { m_scrubField->endScrub(); }
    return true;
}

void PropertyRow::interactionCancelled() {
    if (m_scrubbing && m_scrubField != nullptr) { m_scrubField->endScrub(); }
    m_scrubbing = false;
    Panel::interactionCancelled();
}

} // namespace PandaUI
