#include "PandaUI/Elements/SafeAreaView.hpp"

#include "PandaUI/Runtime/Context.hpp"

namespace PandaUI {

SafeAreaView::SafeAreaView()
    : m_appliedInsets() {
    layout().setWidth(Length::percent(100.f));
    layout().setHeight(Length::percent(100.f));
    layout().setFlexGrow(1.f);
}

void SafeAreaView::viewWillCalculateLayout() {
    Context *context = getContext();
    if (!context) { return; }

    const EdgeInsets insets = context->getSafeAreaInsets();
    if (m_appliedInsets == insets) { return; }

    m_appliedInsets = insets;
    layout().setPadding(Edge::Top, insets.top);
    layout().setPadding(Edge::Right, insets.right);
    layout().setPadding(Edge::Bottom, insets.bottom);
    layout().setPadding(Edge::Left, insets.left);
}

} // namespace PandaUI
