#pragma once

#include "Panda/Extensions/Settings.hpp"

namespace Panda {

// The settings sections of the core, registered through the same registry as the sections of
// the modules (context/plugins.md, decision 2): the project scope — what a frame may cost (the
// shadow maps, the ambient occlusion, the occlusion volume; RenderingSettings.hpp). The game reads
// them (GameContext::getProjectSettings): the export puts them into its manifest. The sections of
// the editor's own (the name of the project, the start world) are registered by the editor.
void registerCoreSettings(SettingsRegistry &registry);

} // namespace Panda
