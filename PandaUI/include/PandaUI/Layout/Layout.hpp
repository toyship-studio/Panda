#pragma once

#include "PandaUI/Core/Base.hpp"

namespace PandaUI {

class View;
struct ViewInternal;

enum class Display {
    Flex,
    None,
};

enum class PositionType {
    Relative,
    Absolute,
};

enum class FlexDirection {
    Column,
    Row,
};

// Whether the children of a row or column flow onto the next line when they do not fit.
enum class Wrap {
    NoWrap,
    Wrap,
    WrapReverse,
};

enum class Align {
    Auto,
    Start,
    Center,
    End,
    Stretch,
};

enum class Justify {
    Start,
    Center,
    End,
    SpaceBetween,
    SpaceAround,
    SpaceEvenly,
};

enum class Edge {
    Left,
    Top,
    Right,
    Bottom,
    Horizontal,
    Vertical,
    All,
};

struct Length {
    enum class Unit {
        Auto,
        Points,
        Percent,
    };

    Unit unit;
    float value;

    static Length autoValue();
    static Length points(float value);
    static Length percent(float value);
};

class Layout {
public:
    Layout();
    explicit Layout(View *view);

    void bind(View *view);
    void reset();

    void setDisplay(Display display);
    void setPositionType(PositionType positionType);
    void setFlexDirection(FlexDirection direction);
    void setFlexWrap(Wrap wrap);
    void setJustifyContent(Justify justify);
    void setAlignItems(Align align);
    void setAlignSelf(Align align);

    void setWidth(Length width);
    void setHeight(Length height);
    void setSize(Size size);
    void setMinWidth(Length width);
    void setMinHeight(Length height);
    void setMaxWidth(Length width);
    void setMaxHeight(Length height);

    void setPadding(float padding);
    void setPadding(Edge edge, float padding);
    void setMargin(float margin);
    void setMargin(Edge edge, float margin);
    void setPosition(Edge edge, Length position);
    void setInsets(EdgeInsets insets);
    void setGap(float gap);

    void setFlexGrow(float flexGrow);
    void setFlexBasis(float basis); // base size before free space is distributed (points)
    float computedPadding(Edge edge) const; // computed padding after layout (specific edges only)
    void setFlexShrink(float flexShrink);

private:
    ViewInternal *internal() const;
    void markDirty();

    View *m_view;
};

} // namespace PandaUI
