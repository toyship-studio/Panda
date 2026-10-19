#pragma once

#include "PandaUI/Elements/Panel.hpp"
#include "PandaUI/Layout/Layout.hpp"

#include <memory>

namespace PandaUI {

enum class StackAxis {
    Vertical,   // children top to bottom (VStack)
    Horizontal, // children left to right (HStack)
};

// A syntactic-sugar container over Panel: lays out children in a column/row with spacing and
// alignment. A thin wrapper over layout() (flexDirection/gap/align) for readable call sites; does
// not scroll and does not virtualize (for many items use CollectionView). VStack/HStack are the
// vertical()/horizontal() factories.
class StackView : public Panel {
public:
    explicit StackView(StackAxis axis = StackAxis::Vertical, float spacing = 0.f);

    void setAxis(StackAxis axis);
    StackAxis axis() const {
        return m_axis;
    }
    void setSpacing(float spacing);
    void setAlignment(Align alignment);    // cross-axis alignment of the children
    void setDistribution(Justify justify); // distribution along the axis

    static std::shared_ptr<StackView> vertical(float spacing = 0.f);
    static std::shared_ptr<StackView> horizontal(float spacing = 0.f);

private:
    StackAxis m_axis;
};

} // namespace PandaUI
