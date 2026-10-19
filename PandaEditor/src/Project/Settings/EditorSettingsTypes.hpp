#pragma once

#include <Panda/Extensions/Settings.hpp>

namespace Panda {

// The sections of the project and editor scopes that the editor itself owns (ProjectSettings.hpp,
// EditorSettings.hpp), registered through the settings registry like the sections of the modules.
// Once per process; the modules register theirs when their parts load.
void registerEditorSettingsTypes(SettingsRegistry &registry);

} // namespace Panda
