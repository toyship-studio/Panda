#include "PandaUI/Elements/Spacer.hpp"

namespace PandaUI {

Spacer::Spacer() {
    setBackgroundColor(Color(0x00000000));
    layout().setFlexGrow(1.f);
}

} // namespace PandaUI
