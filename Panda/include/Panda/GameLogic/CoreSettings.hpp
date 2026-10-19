#pragma once

#include "Panda/Extensions/Settings.hpp"

namespace Panda {

// The settings sections of the core, registered through the same registry as the sections of
// the modules (context/plugins.md, decision 2): the world scope — lighting, graphics, wind.
void registerCoreSettings(SettingsRegistry &registry);

} // namespace Panda
