#pragma once

#include "App/DiskWatch.hpp"
#include "App/DocumentActions.hpp"
#include "App/EditorAppearance.hpp"
#include "App/EditorStatistics.hpp"
#include "App/EditorUpdater.hpp"
#include "App/PlaySession.hpp"
#include "Preview/AssetPreviews.hpp"
#include "Project/Build/ProjectBuilds.hpp"
#include "Project/Loader/ProjectLoader.hpp"
#include "Project/Plugins/PluginStore.hpp"
#include "Project/System/SystemTools.hpp"
#include "UI/Commands.hpp"
#include "UI/EditorMenuBar.hpp"
#include "UI/Host/EditorUI.hpp"
#include "UI/Panels/StartScreen.hpp"
#include "UI/Panels/UpdateIndicator.hpp"
#include "UI/Panels/ViewportView.hpp" // Editor::ScenePointer
#include "Viewport/EditorViewports.hpp"

#include <Panda/Application/Layer.hpp>
#include <Panda/Extensions/Settings.hpp>

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
    bool unattended = false;     // --unattended: nobody is at the screen (SystemTools::UNATTENDED_ARGUMENT)
};

// The editor as an application: it owns the parts of the editor, starts and stops them, gives
// each frame and each event of the window to the one it belongs to, and holds what joins them —
// the project in the window, the world on screen, Play and Stop. What a part does by itself is
// in the part: the game that plays (PlaySession), Save and the reload from disk (DocumentActions), the look
// (EditorAppearance), the viewports (EditorViewports), the builds (ProjectBuilds), the UI of the
// workspace (Editor::EditorController).
class EditorLayer : public Layer,
                    public ProjectLoaderOutput,
                    public FileDocumentsOutput,
                    public EditorMenuBarOutput,
                    public Editor::UpdateIndicatorOutput {
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
    // Save, the reload from disk and the unsaved-changes question over the documents.
    DocumentActions &documentActions() {
        return m_documentActions;
    }
    ProjectLoader &project() {
        return m_loader;
    }
    // The files of the project as the disk holds them, followed: what changes there is taken by itself.
    DiskWatch &diskWatch() {
        return m_diskWatch;
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
    // The theme, the icons of the plugins and the fonts of the editor.
    EditorAppearance &appearance() {
        return m_appearance;
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
    // A dialog of the editor is open over its window.
    bool isModalOpen();

    // ---- The worlds and the game. The open worlds are documents().worlds(); one is active and
    // ---- edited. Play runs a copy of the active world (PlaySession); Stop drops the copy and
    // ---- returns to the world being edited. Each does nothing when it is already so.
    void play();
    void stop();
    bool isPlaying() const {
        return m_play.isPlaying();
    }
    void setGameDriven(bool driven) {
        m_play.setDriven(driven);
    }
    // An event of the input of the game made by an agent (game_input): the UI of the game takes it
    // first, what it leaves goes to Panda::Input — the way a real event goes.
    void postGameEvent(Fern::Event &event);
    bool hasWorld() {
        return m_documents.activeWorld() != nullptr;
    }
    // The document in front is a prefab: edited like a world, never played.
    bool isEditingPrefab() {
        return hasWorld() && m_documents.activeWorld()->isPrefab();
    }
    // The world the frame is made of: the playing copy, the edited world, or — while no world is
    // open — the empty one.
    World *shownWorld() {
        return isPlaying() ? &m_play.world() : &editingWorld();
    }
    // What the panels of the editor read and edit: the world on screen, and nothing while no
    // world is open.
    World *panelsWorld() {
        return isPlaying() || hasWorld() ? shownWorld() : nullptr;
    }
    // Not while the game plays: it plays a copy of the active world, and that world stays.
    void activateWorld(WorldFile &world);
    void closeWorld(WorldFile &world); // drops it from memory; its tab has asked about the edits
    void openWorldAsset(AssetId worldAsset); // a world or a prefab file in a tab of its own (an open one is activated)

    // ---- The settings. The stores of the project and editor scopes; the world's is the context
    // ---- of the world being edited.
    TypeContext &settingsStore(SettingsScope scope);
    std::filesystem::path settingsFilePath(SettingsScope scope);
    void editorSettingsChanged(); // the editor scope was edited: apply (the viewport options) and save

    // ---- The Scene viewport.
    void setGizmoOperation(int mode);    // 0/1/2 = translate / rotate / scale
    void setGizmoSpace(bool worldSpace); // world / local
    void rectSelect(PandaUI::Point start, PandaUI::Point current, bool additive, Editor::RectSelectPhase phase);
    void resetCamera();    // the camera frames the world again
    void focusSelection(); // the camera frames the selected entities

    // ---- The application and the project.
    void closeAppRequest(); // asks about the unsaved edits first
    void restartEditor();   // the same, then a new instance with the same project
    // Another project in this window (File → Open Project, Open Recent, an agent): a folder that
    // is not a project (ProjectLoader::readName) is said so in the log and nothing changes; else
    // asks about the unsaved edits, then the open project is saved and closed and this one takes
    // its place.
    void openProject(const path_t &path);
    // After project.json was reread: a module switched on in it opens the project again (true).
    bool reopenProjectForModules();
    // project.json names a module that is not loaded: the project has to be opened again for it.
    bool isReopenNeededForModules();
    // A module that is loaded is switched off in project.json: the log says a restart drops it.
    void warnOfModulesSwitchedOff();
    // A file of the project outside the editor: in the external editor of the editor settings
    // (the text form of the file: a binary asset opens its sidecar), and in the file manager.
    void openInExternalEditor(const std::filesystem::path &path, int line = 0, int column = 0);
    void revealInFileManager(const std::filesystem::path &path);
    // The second window of the editor, over the main one, with this tree: the Settings, the
    // export of the game. One at a time (EditorController says which); an open one comes to the
    // front. false — the window could not be made.
    bool openAuxiliaryWindow(const char *title, PandaUI::Size size, std::shared_ptr<PandaUI::View> root);
    void closeAuxiliaryWindow();
    // Editor::UpdateIndicatorOutput: the update indicator of the toolbar and of the start screen.
    void updateIndicatorDidActivate() override; // it was clicked: restart, details, release notes
    void updateIndicatorDidDismiss() override;  // hidden until the state of the updater changes

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

    // ---- Once a frame.
    void syncViewports();   // the frames of the Scene and Game tabs → the engine viewports; their textures → the panels
    void updateViewportTools(double deltaTime); // EDIT: the viewport tools (brushes), before the world update
    void updateViewportHelperLines();           // the outlines of the overlays (colliders) → the viewport
    void updateGizmo();     // the gizmo follows the Scene pointer, moves the selection, shows its overlay
    void windowSizeChanged();

    // ---- The worlds and the game.
    // The world the editor edits: the active one of the open worlds (FileDocuments). With no
    // world open it is m_noWorld — an empty world the engine has to draw and the panels to read,
    // so that nothing of the editor needs to know the case; it is not a document and has no tab.
    World &editingWorld() {
        WorldFile *active = m_documents.activeWorld();
        return active != nullptr ? active->world() : m_noWorld;
    }
    void newWorld(); // a new world in a tab of its own, next to the open ones
    // The editor shows another world, or the same one with another content: the selection, the
    // entity tabs and the panels follow.
    void worldDidChange();
    std::string worldDisplayName(); // the name of the open world for the UI (the file / Untitled World); empty — none
    void showDocumentName();        // the chrome is told which document is in front
    // A Build of the scripts succeeded: the editor loads the new library — at once, or when the
    // game stops (a playing game runs the code of the loaded one).
    void scriptsWereBuilt();

    // ---- The project and the window.
    void openProjectRequest();  // asks which folder, then — a project — about the unsaved edits
    void closeProjectRequest(); // asks about the unsaved edits first
    void closeProject();
    void closeApp();
    void newWindow(); // a second instance of the editor, on its start screen
    void saveOpenedProjectWindowState();
    void updateWindowState();
    void syncWindowIdentity(); // the window title and the label of the application icon name the project
    void showStartScreen();
    void showEditorChrome();
    // Editor parts of the project modules (context/plugins.md, stage 2): registered once both an
    // open project and the coordinator (the parts host) exist; new icon folders go into the
    // library, panels with contribution sections are rebuilt.
    void loadEditorParts();
    void applyEditorSettings();

    // ---- The commands of the application (UI/Commands.hpp) and what they do. The layer registers
    // ---- what belongs to the application and the project, the controller what belongs to the workspace.
    void registerCommands();
    void showSettingsFile(SettingsScope scope); // settings.json / project.json in the external editor
    void showKeymapFile();
    void showAbout();

    // ---- The editor updater (App/EditorUpdater): its state goes to the indicators of the toolbar
    // ---- and of the start screen and to the Help menu whenever it changes.
    void checkForUpdates(bool manual);
    void pushUpdateStatus();
    // The buttons of the mouse, for the disk watch.
    void followDiskWatchEvent(const Fern::Event &event);

    World m_noWorld; // what the editor points at while no world is open: empty, never edited
    // The game inside the editor: the playing copy of the active world, its UI and its input.
    // Which camera the frame is seen through is the business of the viewports.
    PlaySession m_play;
    bool m_scriptsBuiltWhilePlaying = false; // the new library is loaded when the game has stopped
    Editor::ScenePointer m_scenePointer; // the pointer over the Scene surface this frame (the gizmo, the tools)
    // Every file the editor holds in memory: the settings of the editor and of the project, the
    // open worlds, the asset documents (Project/Documents/FileDocuments.hpp). Before the loader:
    // the loader opens and closes them with the project and outlives none of them.
    FileDocuments m_documents;
    ProjectLoader m_loader;
    DocumentActions m_documentActions;
    DiskWatch m_diskWatch;
    int m_buttonsHeld = 0; // the buttons of the mouse held in the windows of the editor: the disk watch waits
    EditorUpdater m_updater;
    uint64_t m_shownUpdateRevision = ~uint64_t(0);
    bool m_shownUpdateDismissed = false;
    bool m_shownUpdateReady = false;
    ProjectBuilds m_builds;
    PluginStore m_pluginStore; // installed plugins, the catalog of the portal, the operations (the Plugins tab)
    EditorViewportSettings m_viewportSettings;
    Fern::Window *m_window;
    EditorLaunch m_launch;
    // --unattended: no native dialog for as long as the editor runs.
    std::unique_ptr<SystemTools::Unattended> m_unattended;
    std::string m_windowTitle; // what syncWindowIdentity has set: "<project> — <world file>"
    std::string m_iconLabel;   // the project name on the application icon (empty — none)
    Editor::Commands m_commands; // the commands of the editor and their keys
    EditorMenuBar m_menuBar;
    // The Scene and Game viewports: the targets, the editor camera, the gizmo and the selection, the
    // grid and the helpers, the postprocessing; which one renders this frame (Viewport/EditorViewports.hpp).
    EditorViewports m_viewports;

    // The editor chrome is built entirely on PandaUI. The coordinator is `Editor::EditorController`, panels
    // read the engine directly.
    EditorUI m_editorUI;
    EditorAppearance m_appearance;
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
    std::unique_ptr<AssetPreviews> m_previews; // the pictures of assets: previews of the forms, thumbnails
    EditorStatistics m_statistics; // the Statistics and Profiler panels, the status of the visibility strip
};

} // namespace Panda
