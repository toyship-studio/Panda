#include "PandaUI/Elements/PropertyRow.hpp"

#include "PandaUI/Elements/Label.hpp"

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

    m_title = std::move(title);
    m_label = std::make_shared<Label>(m_title);
    m_label->setNumberOfLines(0); // a long title wraps instead of being cut; the row grows with it
    m_label->layout().setFlexShrink(0.f); // the label keeps the column width
    addSubview(m_label);
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
    m_label->setTextColor(m_appearance.labelColor);
    m_label->layout().setWidth(Length::points(m_appearance.labelWidth));
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
    m_appearance.labelWidth = width;
    m_label->layout().setWidth(Length::points(width));
}

void PropertyRow::setEditor(std::shared_ptr<View> editor, bool fill) {
    if (m_editor) { m_editor->removeFromSuperview(); }
    m_editor = std::move(editor);
    if (!m_editor) { return; }

    if (fill) {
        m_editor->layout().setFlexGrow(1.f);
        m_editor->layout().setFlexShrink(1.f);
    }
    addSubview(m_editor);
}

} // namespace PandaUI
