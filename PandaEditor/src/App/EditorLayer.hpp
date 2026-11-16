#pragma once

#include "App/EditorUpdater.hpp"
#include "UI/Panels/UpdateIndicator.hpp"
#include "Preview/AssetPreviews.hpp"
#include "UI/Commands.hpp"
#include "UI/EditorMenuBar.hpp"
#include "Viewport/EditorViewports.hpp"
#include "Project/Build/ProjectBuilds.hpp"
#include "Project/Loader/ProjectLoader.hpp"
#include "Project/Plugins/PluginStore.hpp"
#include "UI/Host/EditorUI.hpp"
#include "UI/Panels/StartScreen.hpp"
#include "UI/Panels/ViewportView.hpp" // Editor::ScenePointer

#include <Panda/Application/Layer.hpp>
#include <Panda/Extensions/Settings.hpp>
#include <PandaUIGallery/Gallery.hpp> // PandaUI::Gallery::Host
#include <Panda/UI/GameUI.hpp>

#include <cstdint>
#include <functional>
#include <memory>

namespace Editor {
class EditorController; // coordinator of the PandaUI editor
}

namespace Panda {

class EditorMcp; // the MCP server of the editor (App/EditorMcp.hpp)

// How this instance of the editor was started (the command line, App/Main.cpp).
struct EditorLaunch final {
    bool openLastProject = true; // false — a second window: the start screen, not the project of the last session
    path_t project;              // --project <path>: this project instead of the one of the last session
    int mcpPort = 0;             // --mcp <port>: the MCP server listens on it whatever is open; 0 — the project decides
};

class EditorLayer : public Layer,
                    public ProjectLoaderOutput,
                    public FileDocumentsOutput,
                    public EditorMenuBarOutput,
                    public Editor::StartScreenControllerOutput {
public:
    explicit EditorLayer(Fern::Window *window, EditorLaunch launch = {});
    ~EditorLayer() override; // out of line: m_editorController is a unique_ptr to an incomplete type

    virtual void onAttach() override;
    virtual void onDetach() override;

    void onUpdate(double deltaTime) override;
    void onEvent(Fern::Event *event) override;

    // ---- The parts of the editor. Its UI (Editor::EditorController) and the tools that drive it
    // ---- (EditorMcp) work with them directly: the documents, the viewports, the commands.
    FileDocuments &documents() {
        return m_documents;
    }
    ProjectLoader &project() {
        return m_loader;
    }
    // The builds of the project: the scripts, the Export project, the data of the game.
    ProjectBuilds &builds() {
        return m_builds;
    }
    EditorViewports &viewports() {
        return m_viewports;
    }
    EditorUI &ui() {
        return m_editorUI;
    }
    Editor::Commands &commands() {
        return m_commands;
    }
    EditorMenuBar &menuBar() {
        return m_menuBar;
    }
    PluginStore &pluginStore() {
        return m_pluginStore;
    }
    Editor::EditorController *controller() {
        return m_editorController.get();
    }
    Fern::Window *window() {
        return m_window;
    }
    const EditorLaunch &launch() const {
        return m_launch;
    }

    // ---- The worlds. The open ones are documents().worlds(); one is active and edited.
    // Play runs a copy of the active world (physics, scripts); Stop drops the copy and returns to
    // the world being edited. Each does nothing when it is already so.
    void play();
    void stop();
    bool isPlaying() const {
        return m_playing;
    }
    bool hasWorld() {
        return m_documents.activeWorld() != nullptr;
    }
    // What the panels of the editor read and edit: the world on screen — the edited one, or the
    // playing copy — and nothing while no world is open.
    World *panelsWorld() {
        return m_playing || hasWorld() ? m_currentWorld : nullptr;
    }
    // The world the frame is made of: the edited one, the playing copy, or the empty world.
    World *shownWorld() {
        return m_currentWorld;
    }
    // Not while the game plays: it plays a copy of the active world, and that world stays.
    void activateWorld(WorldFile &world);
    bool saveWorld(WorldFile &world); // false — cancelled or failed
    void closeWorld(WorldFile &world); // drops it from memory; its tab has asked about the edits
    void openWorldAsset(AssetId worldAsset); // a world file in a tab of its own (an open one is activated)
    void reloadWorldAsset(AssetId worldAsset); // rereads the world from disk when it is open

    // ---- The file documents: Save, Reload and the unsaved-changes question.
    bool saveAll();                    // every dirty file document; false — cancelled or failed
    bool saveAsset(AssetId asset);     // one asset; false — cancelled or failed
    // The engine and the disk agree again (the Reload button, Cmd+R): changed assets, the script
    // build, the worlds — all by the time of their files (context/architecture.md).
    void reloadFromDisk();
    // The unsaved edits of every document that has a file are dropped: the memory is what the
    // disk holds again. Returns the number of documents read.
    int revertUnsaved();
    bool saveProjectSettings();   // false — the write failed or was cancelled
    void revertProjectSettings(); // the store rereads project.json
    void projectSettingsEdited(); // an edit went into the store: the file is behind
    bool isProjectSettingsDirty();
    // The stores of the project and editor scopes; the world's is the context of the world the
    // panels show (the caller's).
    TypeContext &settingsStore(SettingsScope scope);
    std::filesystem::path settingsFilePath(SettingsScope scope);
    void editorSettingsChanged(); // the editor scope was edited: apply (the viewport options) and save

    // ---- The Scene viewport.
    void setGizmoOperation(int mode);    // 0/1/2 = translate / rotate / scale
    void setGizmoSpace(bool worldSpace); // world / local
    void rectSelect(PandaUI::Point start, PandaUI::Point current, bool additive, Editor::RectSelectPhase phase);
    void resetCamera();    // the camera frames the world again
    void focusSelection(); // the camera frames the selected entities

    // ---- The application.
    void closeAppRequest(); // asks about the unsaved edits first
    void restartEditor();   // the same, then a new instance with the same project
    // Another project in this window (File → Open Recent, an agent): asks about the unsaved edits,
    // then the open project is saved and closed and this one takes its place.
    void openProject(const path_t &path);
    void updateIndicatorAction();  // the update indicator was clicked (restart / details / notes)
    void updateIndicatorDismiss(); // the update indicator was dismissed until the state changes
    // A file of the project outside the editor: in the external editor of the editor settings
    // (the text form of the file: a binary asset opens its sidecar), and in the file manager.
    void openInExternalEditor(const std::filesystem::path &path);
    void revealInFileManager(const std::filesystem::path &path);
    // The Settings window: a second window of the editor over the main one, with this tree. An
    // open one comes to the front. false — the window could not be made.
    bool openSettingsWindow(std::shared_ptr<PandaUI::View> root);
    void closeSettingsWindow();
    // The theme in use (a setting of the editor scope) and the plugins (the Plugins tab).
    std::string currentThemeId();
    void selectTheme(const std::string &id); // applies and saves
    void installDevPlugin();
    // The face of a font asset for the editor's own UI (the preview of the font tab): rasterized
    // once from the bytes of the asset; Sans when it cannot be read.
    PandaUI::FontFamily fontFamilyForAsset(AssetId asset);
    // What the PandaUI gallery (Help → UI Gallery) needs from the UI host of the application.
    PandaUI::Gallery::Host galleryHost();

private:
    enum class UIRootMode {
        None,
        StartScreen,
        Editor,
    };

    // ---- What the parts of the application tell it.
    // ProjectLoaderOutput.
    void loaderDidLoadProject(const std::string &name, const path_t &path) override;
    void loaderWillCloseProject() override;
    void loaderDidLoadCloseProject() override;
    void loaderDidReloadScripts() override;
    // FileDocumentsOutput.
    void documentsDidActivateWorld() override;
    void documentsDidLoadWorld() override;
    void documentsDidReadSettings(SettingsScope scope) override;
    void documentsDidWriteSettings(SettingsScope scope) override;
    EditorCameraSettings documentsEditorCamera() override;
    void documentsSetEditorCamera(const EditorCameraSettings &camera) override;
    void documentsLookAtWorld() override;
    // EditorMenuBarOutput: the lists of the menu bar that are not commands.
    bool menuBarHasOpenedProject() override;
    void menuBarOpenProject(const RecentProject &project) override;
    std::vector<std::string> menuBarNewEntityTitles() override;
    std::vector<std::string> menuBarNewAssetTitles() override;
    void menuBarNewEntity(size_t index) override;
    void menuBarNewAsset(size_t index) override;
    const std::vector<RecentProject> &menuBarGetRecentProjectsList() override;
    const path_t &menuBarGetOpenedProjectPath() override;
    bool menuBarUpdateReady() override;
    // Editor::StartScreenControllerOutput.
    void startScreenDidRequestOpenProjectDialog() override;
    void startScreenDidRequestOpenRecentProject(size_t index) override;
    void startScreenDidRequestRemoveRecentProject(size_t index) override;
    void startScreenDidRequestCreateProject(const std::string &name, const std::string &location) override;
    void startScreenDidRequestChooseProjectLocation() override;
    void startScreenDidActivateUpdate() override;
    void startScreenDidDismissUpdate() override;

    // ---- The commands of the application (UI/Commands.hpp) and what they do. The layer registers
    // ---- what belongs to the application and the project, the controller what belongs to the workspace.
    void registerCommands();
    void newWorld();            // a new world in a tab of its own, next to the open ones
    void newWindow();           // a second instance of the editor, on its start screen
    void openProjectRequest();  // asks about the unsaved edits, then which folder
    void closeProjectRequest(); // asks about the unsaved edits first
    void showSettingsFile(SettingsScope scope); // settings.json / project.json in the external editor
    void showKeymapFile();
    void showAbout();

    // ---- The project and the window.
    void openProjectFolderDialog();
    void closeProject();
    void closeApp();
    void saveOpenedProjectWindowState();
    void updateWindowState();
    void windowSizeChanged();
    void showStartScreen();
    void showEditorChrome();
    void syncStartScreenModel();
    path_t defaultProjectParentPath() const;
    std::string worldDisplayName(); // the name of the open world for the UI (the file / Untitled World); empty — none
    void syncWindowIdentity();      // the window title and the label of the application icon name the project
    // After project.json was reread: a module switched on in it opens the project again (true).
    bool reopenProjectForModules();
    // Editor parts of the project modules (context/plugins.md, stage 2): registered once both an
    // open project and the coordinator (the parts host) exist; new icon folders go into the
    // library, panels with contribution sections are rebuilt.
    void loadEditorParts();
    void applyEditorSettings();

    // ---- The worlds and the game.
    // The world the editor edits: the active one of the open worlds (FileDocuments). With no
    // world open it is m_noWorld — an empty world the engine has to draw and the panels to read,
    // so that nothing of the editor needs to know the case; it is not a document and has no tab.
    World &editingWorld() {
        WorldFile *active = m_documents.activeWorld();
        return active != nullptr ? active->world() : m_noWorld;
    }
    // The editor shows another world, or the same one with another content: the selection, the
    // entity tabs and the panels follow.
    void worldDidChange();
    void requestWorldLoad(AssetId worldAssetId); // the game asked for another world: it is loaded between frames
    void loadPendingWorldRequest();
    bool loadWorldAssetIntoPlayingWorld(AssetId worldAssetId);
    // A Build of the scripts succeeded: the editor loads the new library — at once, or when the
    // game stops (a playing game runs the code of the loaded one).
    void scriptsWereBuilt();

    // ---- The file documents: Save, Reload and the unsaved-changes question (m_documents.list()).
    enum class FileResolution { Saved, Reloaded, Cancelled };
    // The memory has edits and so does the disk (or the disk version is asked for): the user
    // picks the version that stays.
    FileResolution resolveFileConflict(FileDocument &file);
    FileResolution takeDiskVersion(FileDocument &file); // file → memory, with the question when dirty
    void reloadFile(FileDocument &file);                // file → memory, the tabs follow
    bool saveFile(FileDocument &file);                  // memory → file; false — cancelled or failed
    // The gate of everything that drops what is in memory: Save / Don't Save / Cancel.
    void runAfterUnsavedQuestion(std::function<void()> action);

    // ---- Once a frame.
    void syncViewports();   // the frames of the Scene and Game tabs → the engine viewports; their textures → the panels
    void syncGameUiFrame(); // the game UI follows the Game surface
    void updateGameInput(); // the input of the game follows the focus of the Game tab
    bool eventBelongsToGame(Fern::Event *event); // Play: a pointer over the Game surface, a key while the Game tab has the focus
    void updateGizmo();     // the gizmo follows the Scene pointer, moves the selection, shows its overlay
    void updateViewportTools(double deltaTime); // EDIT: the viewport tools (brushes), before the world update
    void updateViewportHelperLines();           // the outlines of the overlays (colliders) → the viewport
    void updateStripStatus(double deltaTime);   // RAM / VRAM / CPU / GPU of the visibility strip, twice a second

    // ---- The editor updater (App/EditorUpdater): its state goes to the indicators of the toolbar
    // ---- and of the start screen and to the Help menu whenever it changes.
    void checkForUpdates(bool manual);
    void pushUpdateStatus();
    Editor::UpdateIndicatorModel updateIndicatorModel() const;
    std::string updateStatusLine() const;

    // ---- The plugins of the editor (data: themes, icons) — the built-in ones from the resources
    // ---- and those of the user folder; read at startup, the theme of the settings is applied
    // ---- before the panels are built.
    void loadPlugins();
    EditorUI::FontSettings fontSettings();       // the fonts of the APPEARANCE settings for the UI host
    void applyTheme(const std::string &themeId); // empty/unknown — the built-in dark one
    void applyIconPlugins();                     // the icons of the installed plugins over the editor's
    // The APPEARANCE section of the editor settings chooses a theme by index.
    std::vector<std::string> themeNames();
    int currentThemeIndex();
    void selectThemeIndex(int index);

    World m_noWorld;       // what the editor points at while no world is open: empty, never edited
    World m_playingWorld;  // Play runs in a copy of the edited world
    World *m_currentWorld; // what the editor shows: the edited world, or the playing copy
    // The game plays: the playing copy of the active world runs (physics, scripts). Which camera
    // the frame is seen through is the business of the viewports (Viewport/EditorViewports.hpp).
    bool m_playing = false;
    bool m_pendingStop = false; // script quit → Stop at the start of the next frame (not in the middle of update)
    bool m_scriptsBuiltWhilePlaying = false; // the new library is loaded when the game has stopped
    bool m_windowFocused = true;    // the editor window has the keyboard focus (Fern WindowFocus / WindowLostFocus)
    bool m_gameInputActive = false; // the game owns the input: Play, the Game tab renders and has the focus
    uint32_t m_gameMouseButtons = 0; // the buttons the game pressed (bit = Fern::MouseButton): their releases are its
    Editor::ScenePointer m_scenePointer; // the pointer over the Scene surface this frame (the gizmo, the tools)
    // Every file the editor holds in memory: the settings of the editor and of the project, the
    // open worlds, the asset documents (Project/Documents/FileDocuments.hpp). Before the loader:
    // the loader opens and closes them with the project and outlives none of them.
    FileDocuments m_documents;
    ProjectLoader m_loader;
    EditorUpdater m_updater;
    uint64_t m_shownUpdateRevision = ~uint64_t(0);
    bool m_shownUpdateDismissed = false;
    bool m_shownUpdateReady = false;
    ProjectBuilds m_builds;
    PluginStore m_pluginStore; // installed plugins, the catalog of the portal, the operations (the Plugins tab)
    EditorViewportSettings m_viewportSettings;
    AssetId m_pendingWorldLoad;
    Fern::Window *m_window;
    EditorLaunch m_launch;
    std::string m_windowTitle; // what syncWindowIdentity has set: "<project> — <world file>"
    std::string m_iconLabel;   // the project name on the application icon (empty — none)
    Editor::Commands m_commands; // the commands of the editor and their keys
    EditorMenuBar m_menuBar;
    GameUI m_gameUI; // the UI of the game while it plays: drawn into the Game surface
    // The Scene and Game viewports: the targets, the editor camera, the gizmo and the selection, the
    // grid and the helpers, the postprocessing; which one renders this frame (Viewport/EditorViewports.hpp).
    EditorViewports m_viewports;

    // The editor chrome is built entirely on PandaUI. The coordinator is `Editor::EditorController`, panels
    // read the engine directly.
    EditorUI m_editorUI;
    std::unique_ptr<Editor::EditorController> m_editorController;
    std::unique_ptr<Editor::StartScreenController> m_startScreenController;
    // The MCP server of the open project (the "mcp" section of project.json): an AI agent looks
    // at the editor and drives it through it.
    std::unique_ptr<EditorMcp> m_mcp;
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
    std::unique_ptr<AssetPreviews> m_previews; // the pictures of assets: previews of the forms, thumbnails

    // The Statistics and Profiler panels: the layout passes of the chrome UI over a one-second
    // window, the refresh of the profiler (0.25 s), the status of the visibility strip.
    uint64_t m_lastUiLayoutCount = 0;
    uint32_t m_uiLayoutAccum = 0;
    uint32_t m_uiLayoutsPerSecond = 0;
    float m_uiLayoutWindow = 0.f;
    float m_profilerRefreshWindow = 0.f;
    double m_stripStatusTimer = 0.0;
    int m_stripFrameCounter = 0;
};

} // namespace Panda
