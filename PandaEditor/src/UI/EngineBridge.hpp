#pragma once

#include <Panda/Base/TypeContext.hpp>
#include <Panda/Extensions/Settings.hpp>
#include <PandaUI/Core/Base.hpp> // PandaUI::Point
#include <PandaUI/Rendering/TextLayout.hpp> // PandaUI::FontFamily

#include <cstdint>
#include <string>

namespace Panda {
class PluginStore; // full type — Project/Plugins/PluginStore.hpp
enum class BuildAction; // full type — Project/Build/ProjectBuilder.hpp
enum class ScriptsBuildConfig;
enum class BuildTargetKind;          // tab of the CMake panel: Scripts | Export
enum class ExportProjectTarget;      // Project/Export/ExportProjectTarget.hpp
} // namespace Panda

namespace Editor {

enum class SceneState; // full type — Toolbar.hpp (needed only on the calling/implementing side)

// Requests of the editor chrome (EditorController) to the engine/host — implemented by EditorLayer. One seam
// instead of a set of injected lambdas: exactly what the editor asks of the engine is visible at a glance.
class EngineBridge {
public:
    virtual ~EngineBridge() = default;

    virtual void save() = 0;                          // serializes the current world
    virtual void openWorldAsset(uint32_t worldAssetId) = 0; // opens a registry world (with the save dialog)
    // Reimport of a world: rereads it from disk if exactly that world is currently open (otherwise a no-op —
    // Reimport does not open worlds). The same path as File → Reload World From Disk.
    virtual void reloadWorldAsset(uint32_t worldAssetId) = 0;
    virtual void setSceneState(SceneState state) = 0; // play / simulate / stop
    // The unified synchronization of the engine with the disk (the Reload button / Cmd+R): changed
    // assets → script build → world, all by mtime (context/architecture.md).
    virtual void reloadFromDisk() = 0;
    virtual void restartEditor() = 0; // the Quit flow (the unsaved-changes dialog included), then a new instance
    virtual void updateIndicatorAction() = 0;  // the update indicator was clicked (restart / details / notes)
    virtual void updateIndicatorDismiss() = 0; // the update indicator was dismissed until the state changes
    virtual void setGizmoOperation(int mode) = 0;     // 0/1/2 = translate / rotate / scale
    virtual void setGizmoSpace(bool worldSpace) = 0;  // world / local
    virtual void rectSelect(PandaUI::Point start, PandaUI::Point current, bool additive, bool committed) = 0; // seam #2
    virtual void resetCamera() = 0;    // camera → origin
    virtual void focusSelection() = 0; // camera → center of the selected entities (Z)
    // Project build (the CMake panel): an asynchronous cmake for the script project or the Export project;
    // the output/result return to the panel through Layer → Controller (appendCMakeOutput /
    // cmakeBuildFinished) once per frame.
    virtual void startScriptsBuild(Panda::BuildAction action, Panda::ScriptsBuildConfig config) = 0;
    virtual void startExportBuild(Panda::BuildAction action, Panda::ExportProjectTarget target) = 0;
    virtual void cancelProjectBuild() = 0;
    // Opens the target's project folder in the file manager (Scripts/<name> or Export).
    virtual void showProjectFolder(Panda::BuildTargetKind kind) = 0;
    // The settings scopes (context/plugins.md): the stores of the project and editor scopes live
    // with the project loader (the world's is the context of the world the panels show).
    virtual Panda::TypeContext &settingsStore(Panda::SettingsScope scope) = 0;
    virtual void editorSettingsChanged() = 0; // the editor scope was edited: apply (viewport options) and save
    virtual bool saveProjectSettings() = 0;   // project.json; false — the write failed
    virtual void revertProjectSettings() = 0; // the store rereads project.json
    // The Plugins tab (UI/Documents/PluginsDocument): the store of the application layer, the theme
    // in use (a setting of the editor scope) and the native folder dialog of Install Dev Plugin.
    virtual Panda::PluginStore &pluginStore() = 0;
    virtual std::string currentThemeId() = 0;            // EditorAppearanceSettings.theme
    virtual void selectTheme(const std::string &id) = 0; // applies and saves
    virtual void installDevPlugin() = 0;
    // The face of a font asset for the editor's own UI (the preview of the font tab): rasterized once
    // by the UI host from the bytes of the asset; Sans when it cannot be read.
    virtual PandaUI::FontFamily fontFamilyForAsset(uint32_t assetId) = 0;
};

} // namespace Editor
