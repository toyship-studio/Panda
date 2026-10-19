#pragma once

#include "PandaUI/Core/Base.hpp"
#include "PandaUI/Elements/Panel.hpp"

namespace PandaUI {

class SafeAreaView : public Panel {
public:
    SafeAreaView();
    void viewWillCalculateLayout() override;

private:
    EdgeInsets m_appliedInsets;
};

} // namespace PandaUI
