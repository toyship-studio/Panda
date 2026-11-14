#pragma once

#include <Panda/Base/TypeContext.hpp>
#include <Panda/Extensions/Settings.hpp>
#include <PandaUI/Core/Base.hpp> // PandaUI::Point
#include <PandaUI/Rendering/TextLayout.hpp> // PandaUI::FontFamily
#include <PandaUIGallery/Gallery.hpp>       // PandaUI::Gallery::Host

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Panda {
class World;
class PluginStore; // full type — Project/Plugins/PluginStore.hpp
enum class BuildAction; // full type — Project/Build/ProjectBuilder.hpp
enum class ScriptsBuildConfig;
enum class BuildTargetKind;          // tab of the CMake panel: Scripts | Export
enum class ExportProjectTarget;      // Project/Export/ExportProjectTarget.hpp
} // namespace Panda

namespace Editor {

enum class SceneState; // full type — Toolbar.hpp (needed only on the calling/implementing side)

// A world the project has open, as its tab sees it (the file document is a WorldFile of
// FileDocuments). The id is stable while the world is open.
struct OpenWorld final {
    uint32_t id = 0;
    std::string name;           // the file name; "Untitled World" for a world without a file
    std::filesystem::path path; // empty — no file yet
    bool dirty = false;
    Panda::World *world = nullptr;
};

// Requests of the editor chrome (EditorController) to the engine/host — implemented by EditorLayer. One seam
// instead of a set of injected lambdas: exactly what the editor asks of the engine is visible at a glance.
class EngineBridge {
public:
    virtual ~EngineBridge() = default;

    // The file documents of the application (Project/Documents/FileDocument.hpp) for their tabs.
    virtual void save() = 0;                      // every unsaved file document (Save All)
    virtual bool saveAsset(uint32_t assetId) = 0; // one asset; false — cancelled or failed
    virtual std::filesystem::path settingsFilePath(Panda::SettingsScope scope) = 0;
    // The worlds the project has open, in the order of their tabs. One of them is active: the
    // viewport, the Hierarchy and the settings sidebar show it, and Play plays its copy.
    virtual std::vector<OpenWorld> openWorlds() = 0;
    virtual uint32_t activeWorldId() = 0;
    // false while the game runs: it plays a copy of the active world, and that world stays.
    virtual bool canActivateWorld() = 0;
    virtual void activateWorld(uint32_t worldId) = 0;
    virtual bool saveWorld(uint32_t worldId) = 0;  // one world; false — cancelled or failed
    virtual void closeWorld(uint32_t worldId) = 0; // drops it from memory; its tab has asked about the edits
    // A file of the project outside the editor: in the external editor of the editor settings
    // (the text form of the file: a binary asset opens its sidecar), and in the file manager.
    virtual void openInExternalEditor(const std::filesystem::path &path) = 0;
    virtual void revealInFileManager(const std::filesystem::path &path) = 0;
    virtual void openWorldAsset(uint32_t worldAssetId) = 0; // a registry world in a tab of its own (an open one is activated)
    // Reimport of a world: rereads it from disk when it is open (otherwise a no-op — Reimport does
    // not open worlds).
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
    // The project settings file document (project.json) for its tab.
    virtual bool saveProjectSettings() = 0;   // false — the write failed or was cancelled
    virtual void revertProjectSettings() = 0; // the store rereads project.json
    virtual void projectSettingsEdited() = 0; // an edit went into the store: the file is behind
    virtual bool isProjectSettingsDirty() = 0;
    // The Plugins tab (UI/Documents/PluginsDocument): the store of the application layer, the theme
    // in use (a setting of the editor scope) and the native folder dialog of Install Dev Plugin.
    virtual Panda::PluginStore &pluginStore() = 0;
    virtual std::string currentThemeId() = 0;            // EditorAppearanceSettings.theme
    virtual void selectTheme(const std::string &id) = 0; // applies and saves
    virtual void installDevPlugin() = 0;
    // The face of a font asset for the editor's own UI (the preview of the font tab): rasterized once
    // by the UI host from the bytes of the asset; Sans when it cannot be read.
    virtual PandaUI::FontFamily fontFamilyForAsset(uint32_t assetId) = 0;
    // The PandaUI gallery (UI/Documents/UiGalleryDocument): what it needs from the UI host of the
    // application — textures from pixels and from the bundle, the serif face of its "Fantasy" skin.
    // No window and no theme capability: the gallery is a tab of the editor window, and the theme
    // is the plugin chosen in Plugins.
    virtual PandaUI::Gallery::Host galleryHost() = 0;
};

} // namespace Editor
