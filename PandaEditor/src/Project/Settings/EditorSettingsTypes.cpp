#include "Project/Settings/EditorSettingsTypes.hpp"
#include "Project/Settings/EditorSettings.hpp"
#include "Project/Settings/ProjectSettings.hpp"

namespace Panda {

namespace {

    // A section of project.json the editor reads itself: the export does not give it to the game
    // (the shader features and the start world reach it in their own fields of the manifest).
    SettingsType editorOwn(SettingsType type) {
        type.editorOnly = true;
        return type;
    }

} // namespace

void registerEditorSettingsTypes(SettingsRegistry &registry) {
    static bool s_registered = false;
    if (s_registered) { return; }
    s_registered = true;
    // In the order the files are written in.
    registry.add(editorOwn(settingsField<&ProjectSettings::name>(SettingsScope::Project, "name")));
    registry.add(editorOwn(settingsField<&ProjectSettings::startWorld>(SettingsScope::Project, "startWorld")));
    registry.add(editorOwn(settingsField<&ProjectSettings::shaderFeatures>(SettingsScope::Project, "shaderFeatures")));
    registry.add(editorOwn(settingsField<&ProjectSettings::disabledModules>(SettingsScope::Project, "disabledModules")));
    registry.add(editorOwn(settingsField<&ProjectSettings::mcp>(SettingsScope::Project, "mcp")));
    registry.add(settingsField<&EditorSettings::viewport>(SettingsScope::Editor, "viewport"));
    registry.add(settingsField<&EditorSettings::appearance>(SettingsScope::Editor, "appearance"));
    registry.add(settingsField<&EditorSettings::updates>(SettingsScope::Editor, "updates"));
    registry.add(settingsField<&EditorSettings::externalTools>(SettingsScope::Editor, "externalTools"));
    registry.add(settingsField<&EditorSettings::toolchain>(SettingsScope::Editor, "toolchain"));
}

} // namespace Panda
