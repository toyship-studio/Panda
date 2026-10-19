#include "PandaUI/Elements/PropertyRow.hpp"

#include "PandaUI/Elements/Label.hpp"

#include <utility>

namespace PandaUI {

namespace {
    constexpr float DEFAULT_LABEL_WIDTH = 120.f;
    constexpr float ROW_MIN_HEIGHT = 28.f;
    constexpr float ROW_GAP = 12.f;
}

PropertyRow::PropertyRow(std::string title)
    : m_labelWidth(DEFAULT_LABEL_WIDTH) {
    setBackgroundColor(Color(0x00000000));
    layout().setFlexDirection(FlexDirection::Row);
    layout().setAlignItems(Align::Center);
    layout().setGap(ROW_GAP);
    layout().setMinHeight(Length::points(ROW_MIN_HEIGHT));

    m_title = std::move(title);
    m_label = std::make_shared<Label>(m_title);
    m_label->setFont(Font(16.f));
    m_label->setTextRole(TextRole::Muted); // form field label — muted (design text/muted)
    m_label->setNumberOfLines(0); // a long title wraps instead of being cut; the row grows with it
    m_label->layout().setWidth(Length::points(m_labelWidth));
    m_label->layout().setFlexShrink(0.f); // the label keeps the column width
    addSubview(m_label);
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
    m_labelWidth = width;
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
