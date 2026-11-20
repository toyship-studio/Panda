#include "Project/Settings/EditorSettingsTypes.hpp"
#include "Project/Settings/EditorSettings.hpp"
#include "Project/Settings/ProjectSettings.hpp"

namespace Panda {

void registerEditorSettingsTypes(SettingsRegistry &registry) {
    static bool s_registered = false;
    if (s_registered) { return; }
    s_registered = true;
    // In the order the files are written in.
    registry.add(settingsField<&ProjectSettings::name>(SettingsScope::Project, "name"));
    registry.add(settingsField<&ProjectSettings::startWorld>(SettingsScope::Project, "startWorld"));
    registry.add(settingsField<&ProjectSettings::shaderFeatures>(SettingsScope::Project, "shaderFeatures"));
    registry.add(settingsField<&ProjectSettings::disabledModules>(SettingsScope::Project, "disabledModules"));
    registry.add(settingsField<&ProjectSettings::mcp>(SettingsScope::Project, "mcp"));
    registry.add(settingsField<&EditorSettings::viewport>(SettingsScope::Editor, "viewport"));
    registry.add(settingsField<&EditorSettings::appearance>(SettingsScope::Editor, "appearance"));
    registry.add(settingsField<&EditorSettings::updates>(SettingsScope::Editor, "updates"));
    registry.add(settingsField<&EditorSettings::externalTools>(SettingsScope::Editor, "externalTools"));
    registry.add(settingsField<&EditorSettings::toolchain>(SettingsScope::Editor, "toolchain"));
}

} // namespace Panda
