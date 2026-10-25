#include "PandaUI/Core/Style.hpp"

#include "PandaUI/Core/Theme.hpp"
#include "PandaUI/Core/View.hpp"

namespace PandaUI {

void applyElevation(View &view, Elevation elevation) {
    view.setBackgroundColor(ThemeColor::surface);
    view.surface().setCornerRadius(Radius::lg);
    view.surface().setBorderWidth(1.f);
    view.surface().setBorderColor(ThemeColor::border);
    view.surface().setShadow(elevation == Elevation::Modal ? Shadow::modal : Shadow::popover);
}

} // namespace PandaUI
