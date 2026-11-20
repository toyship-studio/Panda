#include "Panda/GameLogic/CoreSettings.hpp"
#include "Panda/GameLogic/WorldSettings.hpp"
#include "Panda/Renderer/SceneWind.hpp"

namespace Panda {

void registerCoreSettings(SettingsRegistry &registry) {
    registry.add(settingsType<WorldLightingSettings>(SettingsScope::World, "lighting"));
    registry.add(settingsType<WorldGraphicsSettings>(SettingsScope::World, "graphics"));
    registry.add(settingsType<SceneWindSettings>(SettingsScope::World, "wind"));
}

} // namespace Panda
