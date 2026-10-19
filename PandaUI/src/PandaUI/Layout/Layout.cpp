#include "PandaUI/Layout/Layout.hpp"

#include "PandaUI/Core/View.hpp"
#include "PandaUI/Core/ViewInternal.hpp"

#include <yoga/Yoga.h>

#include <cassert>

namespace PandaUI {

namespace {

    YGEdge toYGEdge(Edge edge) {
        switch (edge) {
            case Edge::Left:
                return YGEdgeLeft;
            case Edge::Top:
                return YGEdgeTop;
            case Edge::Right:
                return YGEdgeRight;
            case Edge::Bottom:
                return YGEdgeBottom;
            case Edge::Horizontal:
                return YGEdgeHorizontal;
            case Edge::Vertical:
                return YGEdgeVertical;
            case Edge::All:
                return YGEdgeAll;
        }
        return YGEdgeAll;
    }

    YGAlign toYGAlign(Align align) {
        switch (align) {
            case Align::Auto:
                return YGAlignAuto;
            case Align::Start:
                return YGAlignFlexStart;
            case Align::Center:
                return YGAlignCenter;
            case Align::End:
                return YGAlignFlexEnd;
            case Align::Stretch:
                return YGAlignStretch;
        }
        return YGAlignAuto;
    }

    YGJustify toYGJustify(Justify justify) {
        switch (justify) {
            case Justify::Start:
                return YGJustifyFlexStart;
            case Justify::Center:
                return YGJustifyCenter;
            case Justify::End:
                return YGJustifyFlexEnd;
            case Justify::SpaceBetween:
                return YGJustifySpaceBetween;
            case Justify::SpaceAround:
                return YGJustifySpaceAround;
            case Justify::SpaceEvenly:
                return YGJustifySpaceEvenly;
        }
        return YGJustifyFlexStart;
    }

    YGNodeRef nodeFor(ViewInternal *viewInternal) {
        if (!viewInternal) { return nullptr; }
        return viewInternal->ref;
    }

    void setWidth(YGNodeRef node, Length width) {
        switch (width.unit) {
            case Length::Unit::Auto:
                YGNodeStyleSetWidthAuto(node);
                break;
            case Length::Unit::Points:
                YGNodeStyleSetWidth(node, width.value);
                break;
            case Length::Unit::Percent:
                YGNodeStyleSetWidthPercent(node, width.value);
                break;
        }
    }

    void setHeight(YGNodeRef node, Length height) {
        switch (height.unit) {
            case Length::Unit::Auto:
                YGNodeStyleSetHeightAuto(node);
                break;
            case Length::Unit::Points:
                YGNodeStyleSetHeight(node, height.value);
                break;
            case Length::Unit::Percent:
                YGNodeStyleSetHeightPercent(node, height.value);
                break;
        }
    }

    void setMinWidth(YGNodeRef node, Length width) {
        switch (width.unit) {
            case Length::Unit::Auto:
                YGNodeStyleSetMinWidth(node, YGUndefined);
                break;
            case Length::Unit::Points:
                YGNodeStyleSetMinWidth(node, width.value);
                break;
            case Length::Unit::Percent:
                YGNodeStyleSetMinWidthPercent(node, width.value);
                break;
        }
    }

    void setMinHeight(YGNodeRef node, Length height) {
        switch (height.unit) {
            case Length::Unit::Auto:
                YGNodeStyleSetMinHeight(node, YGUndefined);
                break;
            case Length::Unit::Points:
                YGNodeStyleSetMinHeight(node, height.value);
                break;
            case Length::Unit::Percent:
                YGNodeStyleSetMinHeightPercent(node, height.value);
                break;
        }
    }

    void setMaxWidth(YGNodeRef node, Length width) {
        switch (width.unit) {
            case Length::Unit::Auto:
                YGNodeStyleSetMaxWidth(node, YGUndefined);
                break;
            case Length::Unit::Points:
                YGNodeStyleSetMaxWidth(node, width.value);
                break;
            case Length::Unit::Percent:
                YGNodeStyleSetMaxWidthPercent(node, width.value);
                break;
        }
    }

    void setMaxHeight(YGNodeRef node, Length height) {
        switch (height.unit) {
            case Length::Unit::Auto:
                YGNodeStyleSetMaxHeight(node, YGUndefined);
                break;
            case Length::Unit::Points:
                YGNodeStyleSetMaxHeight(node, height.value);
                break;
            case Length::Unit::Percent:
                YGNodeStyleSetMaxHeightPercent(node, height.value);
                break;
        }
    }

    void setPosition(YGNodeRef node, YGEdge edge, Length position) {
        switch (position.unit) {
            case Length::Unit::Auto:
                YGNodeStyleSetPosition(node, edge, YGUndefined);
                break;
            case Length::Unit::Points:
                YGNodeStyleSetPosition(node, edge, position.value);
                break;
            case Length::Unit::Percent:
                YGNodeStyleSetPositionPercent(node, edge, position.value);
                break;
        }
    }

} // namespace

Length Length::autoValue() {
    return {Unit::Auto, 0.f};
}

Length Length::points(float value) {
    return {Unit::Points, value};
}

Length Length::percent(float value) {
    assert((value >= 0.f && value <= 100.f) && "Percent values should be between 0 and 100");
    return {Unit::Percent, value};
}

Layout::Layout()
    : m_view(nullptr) {}

Layout::Layout(View *view)
    : m_view(view) {}

void Layout::bind(View *view) {
    m_view = view;
}

ViewInternal *Layout::internal() const {
    if (!m_view) { return nullptr; }
    return m_view->m_internal.get();
}

void Layout::reset() {
    YGNodeRef node = nodeFor(internal());
    if (!node) { return; }
    YGNodeReset(node);
    m_view->configureYogaNode();
    m_view->resolveHierarchy();
    markDirty();
}

void Layout::setDisplay(Display display) {
    YGNodeRef node = nodeFor(internal());
    if (!node) { return; }
    const YGDisplay value = display == Display::Flex ? YGDisplayFlex : YGDisplayNone;
    if (YGNodeStyleGetDisplay(node) == value) { return; } // idempotency: do not dirty the layout needlessly
    YGNodeStyleSetDisplay(node, value);
    markDirty();
}

void Layout::setPositionType(PositionType positionType) {
    YGNodeRef node = nodeFor(internal());
    if (!node) { return; }
    YGNodeStyleSetPositionType(
        node, positionType == PositionType::Absolute ? YGPositionTypeAbsolute : YGPositionTypeRelative
    );
    markDirty();
}

void Layout::setFlexDirection(FlexDirection direction) {
    YGNodeRef node = nodeFor(internal());
    if (!node) { return; }
    YGNodeStyleSetFlexDirection(node, direction == FlexDirection::Row ? YGFlexDirectionRow : YGFlexDirectionColumn);
    markDirty();
}

void Layout::setFlexWrap(Wrap wrap) {
    YGNodeRef node = nodeFor(internal());
    if (!node) { return; }
    YGNodeStyleSetFlexWrap(node, wrap == Wrap::Wrap ? YGWrapWrap : wrap == Wrap::WrapReverse ? YGWrapWrapReverse : YGWrapNoWrap);
    markDirty();
}

void Layout::setJustifyContent(Justify justify) {
    YGNodeRef node = nodeFor(internal());
    if (!node) { return; }
    YGNodeStyleSetJustifyContent(node, toYGJustify(justify));
    markDirty();
}

void Layout::setAlignItems(Align align) {
    YGNodeRef node = nodeFor(internal());
    if (!node) { return; }
    YGNodeStyleSetAlignItems(node, toYGAlign(align));
    markDirty();
}

void Layout::setAlignSelf(Align align) {
    YGNodeRef node = nodeFor(internal());
    if (!node) { return; }
    YGNodeStyleSetAlignSelf(node, toYGAlign(align));
    markDirty();
}

void Layout::setWidth(Length width) {
    YGNodeRef node = nodeFor(internal());
    if (!node) { return; }
    PandaUI::setWidth(node, width);
    markDirty();
}

void Layout::setHeight(Length height) {
    YGNodeRef node = nodeFor(internal());
    if (!node) { return; }
    PandaUI::setHeight(node, height);
    markDirty();
}

void Layout::setSize(Size size) {
    YGNodeRef node = nodeFor(internal());
    if (!node) { return; }
    YGNodeStyleSetWidth(node, size.width);
    YGNodeStyleSetHeight(node, size.height);
    markDirty();
}

void Layout::setMinWidth(Length width) {
    YGNodeRef node = nodeFor(internal());
    if (!node) { return; }
    PandaUI::setMinWidth(node, width);
    markDirty();
}

void Layout::setMinHeight(Length height) {
    YGNodeRef node = nodeFor(internal());
    if (!node) { return; }
    PandaUI::setMinHeight(node, height);
    markDirty();
}

void Layout::setMaxWidth(Length width) {
    YGNodeRef node = nodeFor(internal());
    if (!node) { return; }
    PandaUI::setMaxWidth(node, width);
    markDirty();
}

void Layout::setMaxHeight(Length height) {
    YGNodeRef node = nodeFor(internal());
    if (!node) { return; }
    PandaUI::setMaxHeight(node, height);
    markDirty();
}

void Layout::setPadding(float padding) {
    setPadding(Edge::All, padding);
}

void Layout::setPadding(Edge edge, float padding) {
    YGNodeRef node = nodeFor(internal());
    if (!node) { return; }
    YGNodeStyleSetPadding(node, toYGEdge(edge), padding);
    markDirty();
}

void Layout::setMargin(float margin) {
    setMargin(Edge::All, margin);
}

void Layout::setMargin(Edge edge, float margin) {
    YGNodeRef node = nodeFor(internal());
    if (!node) { return; }
    YGNodeStyleSetMargin(node, toYGEdge(edge), margin);
    markDirty();
}

void Layout::setPosition(Edge edge, Length position) {
    YGNodeRef node = nodeFor(internal());
    if (!node) { return; }
    PandaUI::setPosition(node, toYGEdge(edge), position);
    markDirty();
}

void Layout::setInsets(EdgeInsets insets) {
    YGNodeRef node = nodeFor(internal());
    if (!node) { return; }
    YGNodeStyleSetPosition(node, YGEdgeTop, insets.top);
    YGNodeStyleSetPosition(node, YGEdgeRight, insets.right);
    YGNodeStyleSetPosition(node, YGEdgeBottom, insets.bottom);
    YGNodeStyleSetPosition(node, YGEdgeLeft, insets.left);
    markDirty();
}

void Layout::setGap(float gap) {
    YGNodeRef node = nodeFor(internal());
    if (!node) { return; }
    YGNodeStyleSetGap(node, YGGutterAll, gap);
    markDirty();
}

void Layout::setFlexGrow(float flexGrow) {
    YGNodeRef node = nodeFor(internal());
    if (!node) { return; }
    YGNodeStyleSetFlexGrow(node, flexGrow);
    markDirty();
}

float Layout::computedPadding(Edge edge) const {
    YGNodeRef node = nodeFor(internal());
    if (!node) { return 0.f; }
    switch (edge) {
        case Edge::Left: return YGNodeLayoutGetPadding(node, YGEdgeLeft);
        case Edge::Top: return YGNodeLayoutGetPadding(node, YGEdgeTop);
        case Edge::Right: return YGNodeLayoutGetPadding(node, YGEdgeRight);
        case Edge::Bottom: return YGNodeLayoutGetPadding(node, YGEdgeBottom);
        case Edge::Horizontal:
        case Edge::Vertical:
        case Edge::All: break; // composite edges have no single computed value
    }
    return 0.f;
}

void Layout::setFlexBasis(float basis) {
    YGNodeRef node = nodeFor(internal());
    if (!node) { return; }
    YGNodeStyleSetFlexBasis(node, basis);
    markDirty();
}

void Layout::setFlexShrink(float flexShrink) {
    YGNodeRef node = nodeFor(internal());
    if (!node) { return; }
    YGNodeStyleSetFlexShrink(node, flexShrink);
    markDirty();
}

void Layout::markDirty() {
    if (m_view) { m_view->setNeedsLayout(); }
}

} // namespace PandaUI
