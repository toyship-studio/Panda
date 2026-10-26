#pragma once
#include "Bamboo/Base.hpp"
#include <vector>

namespace Bamboo::FontAPI {

// The bytes of the font file behind the asset (.ttf / .otf / .ttc as it lies in the project): what a
// rasterizer needs. Empty — no such asset, or not a font. The UI registers fonts itself
// (Bamboo::UI::registerFont); this is for anything else that reads a font.
std::vector<unsigned char> readData(FontHandle font);

} // namespace Bamboo::FontAPI
