#include "Project/Settings/EditorSettingsTypes.hpp"
#include "Project/Settings/EditorSettings.hpp"
#include "Project/Settings/ProjectSettings.hpp"

namespace Panda {

void registerEditorSettingsTypes(SettingsRegistry &registry) {
    static bool s_registered = false;
    if (s_registered) { return; }
    s_registered = true;
    registry.add(settingsType<ProjectWorldSettings>(SettingsScope::Project, "world"));
    registry.add(settingsType<ProjectModuleSettings>(SettingsScope::Project, "modules"));
    registry.add(settingsType<EditorViewportSettings>(SettingsScope::Editor, "viewport"));
    registry.add(settingsType<EditorAppearanceSettings>(SettingsScope::Editor, "appearance"));
    registry.add(settingsType<EditorUpdateSettings>(SettingsScope::Editor, "updates"));
}

} // namespace Panda
