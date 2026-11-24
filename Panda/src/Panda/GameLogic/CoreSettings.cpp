#include "Panda/GameLogic/CoreSettings.hpp"
#include "Panda/Renderer/OcclusionVolume.hpp"
#include "Panda/Renderer/RenderingSettings.hpp"

namespace Panda {

void registerCoreSettings(SettingsRegistry &registry) {
    registry.add(settingsType<ShadowSettings>(SettingsScope::Project, "shadows"));
    registry.add(settingsType<AmbientOcclusionSettings>(SettingsScope::Project, "ambientOcclusion"));
    registry.add(settingsType<OcclusionVolume::Settings>(SettingsScope::Project, "occlusionVolume"));
}

} // namespace Panda
