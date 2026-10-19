#pragma once

#include <Foundation/PlatformDetection.hpp>

namespace Panda {

// Hooks the system color eyedropper up to PandaUI::ColorPicker (macOS — NSColorSampler:
// a magnifier over the whole screen). On platforms without an implementation it is a no-op, the
// eyedropper button in the picker is simply not shown.
#ifdef PLATFORM_MACOS
void installSystemColorSampler();
#else
inline void installSystemColorSampler() {}
#endif

} // namespace Panda
