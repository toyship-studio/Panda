#include "Panda/GameLogic/CoreSettings.hpp"
#include "Panda/GameLogic/WorldSettings.hpp"

namespace Panda {

void registerCoreSettings(SettingsRegistry &registry) {
    registry.add(settingsField<&WorldSettings::lighting>(SettingsScope::World, "lighting"));
    registry.add(settingsField<&WorldSettings::graphics>(SettingsScope::World, "graphics"));
    registry.add(settingsField<&WorldSettings::wind>(SettingsScope::World, "wind"));
}

} // namespace Panda
