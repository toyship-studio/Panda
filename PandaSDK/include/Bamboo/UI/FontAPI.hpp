#pragma once
#include "Bamboo/Base.hpp"
#include <PandaUI/PandaUI.hpp>

namespace Bamboo::UI {

// A font asset of the project (a FontHandle field of the script, chosen in the inspector) in the UI of
// the script. registerFont rasterizes the face once, puts it into every window of the script — the
// ones that exist and the ones created later — and returns the family for a PandaUI::Font:
//
//     PANDA_FIELD(titleFont)                              // Bamboo::FontHandle, set in the inspector
//     label->setFont(Bamboo::UI::font(titleFont, 24.f));  // or PandaUI::Font(24.f, weight, registerFont(titleFont))
//
// A font file is one weight: Medium and Bold of the family resolve to the same face. Characters the
// face lacks are drawn with the built-in font. The same handle registers once; an invalid handle or
// an asset that is not a font gives the built-in Sans.
PandaUI::FontFamily registerFont(FontHandle font);
PandaUI::Font font(FontHandle font, float size, PandaUI::FontWeight weight = PandaUI::FontWeight::Regular);

} // namespace Bamboo::UI
