#include "PandaUI/Elements/StackView.hpp"

#include <memory>

namespace PandaUI {

StackView::StackView(StackAxis axis, float spacing)
    : m_axis(axis) {
    setBackgroundColor(Color(0x00000000));
    layout().setFlexDirection(axis == StackAxis::Vertical ? FlexDirection::Column : FlexDirection::Row);
    layout().setGap(spacing);
}

void StackView::setAxis(StackAxis axis) {
    m_axis = axis;
    layout().setFlexDirection(axis == StackAxis::Vertical ? FlexDirection::Column : FlexDirection::Row);
}

void StackView::setSpacing(float spacing) {
    layout().setGap(spacing);
}

void StackView::setAlignment(Align alignment) {
    layout().setAlignItems(alignment);
}

void StackView::setDistribution(Justify justify) {
    layout().setJustifyContent(justify);
}

std::shared_ptr<StackView> StackView::vertical(float spacing) {
    return std::make_shared<StackView>(StackAxis::Vertical, spacing);
}

std::shared_ptr<StackView> StackView::horizontal(float spacing) {
    return std::make_shared<StackView>(StackAxis::Horizontal, spacing);
}

} // namespace PandaUI
