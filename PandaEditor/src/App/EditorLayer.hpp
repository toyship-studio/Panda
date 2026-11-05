#pragma once

#include "App/EditorUpdater.hpp"
#include "UI/Panels/UpdateIndicator.hpp"
#include "App/SceneState.hpp"
#include "Preview/ThumbnailsManager.hpp"
#include "UI/EditorMenuBar.hpp"
#include "Viewport/EditorViewports.hpp"
#include "UI/EngineBridge.hpp"
#include "Project/Build/ProjectBuilder.hpp"
#include "Project/Loader/ProjectLoader.hpp"
#include "Project/Plugins/PluginStore.hpp"
#include "UI/Host/EditorUI.hpp"
#include "UI/Panels/StartScreen.hpp"
#include "UI/Panels/ViewportView.hpp" // Editor::ScenePointer

#include <Panda/Application/Layer.hpp>
#include <Panda/UI/PandaUIImpl.hpp>

#include <cstdint>
#include <functional>
#include <memory>

namespace Editor {
class EditorController; // coordinator of the PandaUI editor
}

namespace Panda {

class EditorLayer : public Layer,
                    public ProjectLoaderOutput,
                    public EditorMenuBarOutput,
                    public Editor::EngineBridge,
                    public Editor::StartScreenControllerOutput {
public:
    explicit EditorLayer(Fern::Window *window);
    ~EditorLayer() override; // out of line: m_editorController is a unique_ptr to an incomplete type

    virtual void onAttach() override;
    virtual void onDetach() override;

    void onUpdate(double deltaTime) override;
    void onEvent(Fern::Event *event) override;

    void play();
    void stop();

#pragma region Project loader output
    void loaderDidLoadProject(const std::string &name, const path_t &path) override;
    std::string worldDisplayName(); // name of the open world for the UI (file stem / Untitled World)
    // Layout passes of the chrome UI over a one-second window (Statistics panel).
    uint64_t m_lastUiLayoutCount = 0;
    uint32_t m_uiLayoutAccum = 0;
    uint32_t m_uiLayoutsPerSecond = 0;
    float m_uiLayoutWindow = 0.f;
    float m_profilerRefreshWindow = 0.f; // Profiler panel throttling (0.25 s)
    void loaderDidLoadWorld() override;
    void loaderDidLoadCloseProject() override;
    void loaderDidCreateNewWorld() override;
    EditorCameraSettings getEditorCameraSettings() override;
    void setEditorCameraSettings(EditorCameraSettings settings) override;
#pragma endregion

private:
    void updateStripStatus(double deltaTime);
    double m_stripStatusTimer = 0.0;
    int m_stripFrameCounter = 0;
    enum class UIRootMode {
        None,
        StartScreen,
        Editor,
    };

    void windowSizeChanged(Size size);
    void closeProject();
    void closeAppRequest();
    void closeApp();
    void saveOpenedProjectWindowState();
    bool saveWorld();
    void reloadOpenedWorld(); // re-read the current world from disk (menu and Reimport)
    void runWithSaveCurrentWorldDialog(std::function<void()> action);
    bool cookRuntimeData();
    void openExportProject(ExportProjectTarget target);
    void updateWindowState();
    void openProjectFolderDialog();
    void requestWorldLoad(AssetId worldAssetId);
    void loadPendingWorldRequest();
    bool loadWorldAssetIntoPlayingWorld(AssetId worldAssetId);
    void showStartScreen();
    void showEditorChrome();
    void syncStartScreenModel();
    path_t defaultProjectParentPath() const;
    void syncPandaUIViewportFrame();    // the game UI follows the Game surface
    void syncViewports();               // the frames of the Scene and Game tabs → the engine viewports; their textures → the panels
    void updateGameInput();             // once a frame: the input of the game follows the focus of the Game tab
    bool eventBelongsToGame(Fern::Event *event); // Play: a pointer over the Game surface, a key while the Game tab has the focus
    void drivePandaUIGizmo(); // seam #3: feed the gizmo from the Scene pointer + manipulate + show/hide overlay
    // EngineBridge — requests from the editor chrome to the engine/host.
    void save() override;
    void openWorldAsset(uint32_t worldAssetId) override;
    void reloadWorldAsset(uint32_t worldAssetId) override;
    void setSceneState(Editor::SceneState state) override;
    void reloadFromDisk() override;
    void restartEditor() override;
    void updateIndicatorAction() override;
    void updateIndicatorDismiss() override;
    void setGizmoOperation(int mode) override;
    void setGizmoSpace(bool worldSpace) override;
    void rectSelect(PandaUI::Point start, PandaUI::Point current, bool additive, bool committed) override; // seam #2
    void resetCamera() override;
    void focusSelection() override;
    void startScriptsBuild(BuildAction action, ScriptsBuildConfig config) override;
    void startExportBuild(BuildAction action, ExportProjectTarget target) override;
    void cancelProjectBuild() override;
    void showProjectFolder(BuildTargetKind kind) override;
    Panda::TypeContext &settingsStore(Panda::SettingsScope scope) override;
    void editorSettingsChanged() override;
    bool saveProjectSettings() override;
    void revertProjectSettings() override;
    Panda::PluginStore &pluginStore() override;
    std::string currentThemeId() override;
    void selectTheme(const std::string &id) override; // the Plugins tab: store, apply, save
    void installDevPlugin() override;
    PandaUI::FontFamily fontFamilyForAsset(uint32_t assetId) override;
    PandaUI::Gallery::Host galleryHost() override;
    // Editor parts of the project modules (context/plugins.md, stage 2): registered once both an
    // open project and the coordinator (the parts host) exist; new icon folders go into the
    // library, panels with contribution sections are rebuilt.
    void loadEditorParts();
    void updateViewportTools(double deltaTime); // EDIT: viewport tools (brushes) before the world update
    void pumpProjectBuild(); // once per frame: output lines → CMake panel; on scripts Build success — reload dylib
    void updateViewportHelperLines(); // once per frame: wireframe collider outlines → viewport overlay
    bool menuBarHasOpenedProject() override;
    void menuBarOpenProject() override;
    void menuBarOpenProject(const RecentProject &project) override;
    void menuBarNewWorld() override;
    void menuBarUndo() override;
    void menuBarRedo() override;
    void menuBarOpenWorldSettings() override;
    void menuBarOpenProjectSettings() override;
    void menuBarOpenEditorSettings() override;
    void menuBarShowProjectSettingsFile() override;
    void menuBarShowEditorSettingsFile() override;
    const std::vector<RecentProject> &menuBarGetRecentProjectsList() override;
    const path_t &menuBarGetOpenedProjectPath() override;
    void menuBarCloseApp() override;
    void menuBarShowCppProject() override;
    void menuBarSaveWorld() override;
    void menuBarReload() override;
    void menuBarCloseProject() override;
    void menuBarAbout() override;
    void menuBarCheckForUpdates() override;
    void menuBarRestartToUpdate() override;
    void menuBarViewReleaseNotes() override;
    bool menuBarUpdateReady() override;
    void menuBarExportGame() override;
    void menuBarOpenExportProject(ExportProjectTarget target) override;
    void menuBarOpenPlugins() override;
    void menuBarOpenUiGallery() override;
    void menuBarOpenGameView() override;

    void startScreenDidRequestOpenProjectDialog() override;
    void startScreenDidRequestOpenRecentProject(size_t index) override;
    void startScreenDidRequestRemoveRecentProject(size_t index) override;
    void startScreenDidRequestCreateProject(const std::string &name, const std::string &location) override;
    void startScreenDidRequestChooseProjectLocation() override;
    void startScreenDidActivateUpdate() override;
    void startScreenDidDismissUpdate() override;

    // The editor updater (App/EditorUpdater): its state goes to the indicators of the toolbar and
    // of the start screen and to the Help menu whenever it changes.
    void checkForUpdates(bool manual);
    void pushUpdateStatus();
    Editor::UpdateIndicatorModel updateIndicatorModel() const;
    std::string updateStatusLine() const;

    World m_editingWorld;
    World m_playingWorld;
    World *m_currentWorld;
    SceneState m_sceneState;
    bool m_pendingStop = false; // script quit → Stop at the start of the next frame (not in the middle of update)
    bool m_windowFocused = true;    // the editor window has the keyboard focus (Fern WindowFocus / WindowLostFocus)
    bool m_gameInputActive = false; // the game owns the input: Play, the Game tab renders and has the focus
    uint32_t m_gameMouseButtons = 0; // the buttons the game pressed (bit = Fern::MouseButton): their releases are its
    Editor::ScenePointer m_scenePointer; // the pointer over the Scene surface this frame (the gizmo, the tools)
    ProjectLoader m_loader;
    EditorUpdater m_updater;
    uint64_t m_shownUpdateRevision = ~uint64_t(0);
    bool m_shownUpdateDismissed = false;
    bool m_shownUpdateReady = false;
    ProjectBuilder m_projectBuilder;
    // Editor plugins (data: themes) — built-in ones from the resources and the user folder;
    // read at startup, the theme from the settings is applied before the panels are built.
    PluginStore m_pluginStore; // installed plugins, the catalog of the portal, the operations (the Plugins tab)
    void loadPlugins();
    EditorUI::FontSettings fontSettings();       // the fonts of the APPEARANCE settings for the UI host
    void applyTheme(const std::string &themeId); // empty/unknown — the built-in dark one
    void applyIconPlugins();                     // the icons of the installed plugins over the editor's
    // The APPEARANCE section of the editor settings chooses a theme by index.
    std::vector<std::string> themeNames();
    int currentThemeIndex();
    void selectThemeIndex(int index);
    EditorViewportSettings m_viewportSettings;
    AssetId m_pendingWorldLoad;
    Fern::Window *m_window;
    EditorMenuBar m_menuBar;
    PandaUIImpl m_pandaUI;
    // The Scene and Game viewports: the targets, the editor camera, the gizmo and the selection, the
    // grid and the helpers, the postprocessing; which one renders this frame (Viewport/EditorViewports.hpp).
    EditorViewports m_viewports;

    // The editor chrome is built entirely on PandaUI. The coordinator is `Editor::EditorController`, panels
    // read the engine directly.
    EditorUI m_editorUI;
    std::unique_ptr<Editor::EditorController> m_editorController;
    std::unique_ptr<Editor::StartScreenController> m_startScreenController;
    UIRootMode m_uiRootMode = UIRootMode::None;
    PandaUI::TextureHandle m_sceneTexture; // the output of the Scene surface wrapped for its PandaUI panel
    Miren::TextureHandle m_sceneNative;    // last wrapped native handle — re-wrap only if it changes
    PandaUI::TextureHandle m_gameTexture;  // the same for the Game surface
    Miren::TextureHandle m_gameNative;
    // The PandaUI gallery (Help → UI Gallery): its logo and the serif face of its skin are loaded
    // once, on the first opening, and stay for the process.
    PandaUI::TextureHandle m_galleryLogo;
    PandaUI::FontFamily m_galleryDisplayFont = PandaUI::FontFamily::Sans;
    bool m_galleryFontSearched = false;

    // Asset previews/thumbnails: the entire cache and rendering are in the manager, panels go through
    // shared().
    std::unique_ptr<ThumbnailsManager> m_thumbnails;
};

} // namespace Panda
