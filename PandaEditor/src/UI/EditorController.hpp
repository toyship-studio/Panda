#pragma once

#include "UI/Panels/ContentBrowserView.hpp"
#include "UI/Panels/CMakePanel.hpp"
#include "UI/Panels/OutputPanel.hpp"
#include "UI/Panels/HierarchyPanel.hpp"
#include "UI/Panels/SettingsSectionsView.hpp"
#include "UI/Panels/StatisticsPanel.hpp"
#include "UI/Documents/EntityEditorView.hpp"
#include "UI/Panels/ProfilerPanel.hpp"
#include "UI/Panels/Toolbar.hpp"
#include "UI/Panels/ViewportPanel.hpp"
#include "UI/Panels/GameViewportView.hpp"
#include <PandaEditorKit/Widgets/EditorView.hpp>
#include "UI/DockManager.hpp"
#include "UI/EditorTabs.hpp"
#include "UI/Commands.hpp"

#include <PandaEditorKit/Document.hpp>
#include <PandaEditorKit/EditorExtensions.hpp>
#include <PandaEditorKit/EditorHost.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace Panda {
class World;
class CommandHistory;
class EditorLayer;
class WorldFile;
} // namespace Panda

namespace Editor {

class AssetActions;
class EntityActions;
class GameDocument;
class ExportWindow;
class SettingsWindow;
enum class SettingsPage;
class WorldTabs;

// The on-screen frames of the two viewport tabs (points): what the engine renders into and which
// of them is visible this frame (the selected tab of a visible dock).
struct ViewportSurfaces {
    PandaUI::Rect scene;
    bool sceneVisible = false;
    PandaUI::Rect game;
    bool gameVisible = false;
    // The viewport the user works in — the one that renders while both are on screen: the Game
    // (true) or the Scene. It is the one whose surface had the focus last; Play gives it to the
    // Game, Stop gives it back to the Scene.
    bool gameInFocus = false;
};

// The editor COORDINATOR — the workspace of an open project: it builds the chrome (the panels, the
// docks, the tabs), keeps it in step with the application once a frame and is the host of the
// editor parts of packages (Editor::EditorHost, PandaEditorKit). The panels read the engine
// DIRECTLY (entities by UUID from World, assets from the registry, the selection —
// Editor::selection()); there is no intermediate view-model. What the user does goes to the part
// it belongs to, each the Output of its panels: the entities — EntityActions, the assets —
// AssetActions, the open worlds — WorldTabs, the builds — Panda::ProjectBuilds; the toolbar and
// the viewport overlay are answered here. What a part needs of the application — the open worlds,
// Save, Play — it asks of the application itself (Panda::EditorLayer), with no interface in between.
class EditorController final : public Editor::EditorHost,
                               public ViewportPanelOutput,
                               public ToolbarOutput,
                               public EditorTabsHost {
public:
    explicit EditorController(Panda::EditorLayer &app);
    ~EditorController() override; // unbinds the process host (Editor::setEditorHost(nullptr))

    std::shared_ptr<PandaUI::View> rootView() const {
        return m_root;
    }

    // ---- The world the panels show. The application tells when it is another one.
    void setWorld(Panda::World *world);       // a project opened: the world of the panels, the assets of the project
    void setActiveWorld(Panda::World *world); // Play / Stop / another world in front: the panels show this one
    // The document in front: its name is the root of the Hierarchy, its file the caption of the
    // Hierarchy tab (`world.world`, `Tree.prefab`; empty — no document). `prefabRoot` — of a
    // prefab: its root entity, the one tree the Hierarchy shows (0 — a world: all of it).
    void setWorldDisplayName(const std::string &name, const std::string &file, Panda::UUID prefabRoot);
    // Another world, another project or none: the entity tabs point into the previous world —
    // they close; the navigation history points at what is gone — it is cleared.
    void worldContextChanged();

    // ---- Once a frame, from the application.
    void syncSelection();      // what the viewport selected or moved → the panels and the tabs
    void syncWorldStructure(); // the game created or destroyed entities → the Hierarchy, by the structure version
    void updateChromeFromState(bool isEditMode); // the toolbar, the world tabs, the unsaved dots of the tabs
    void setFrameStatistics(
        int fps, uint32_t drawCalls3d, uint32_t shadowDrawCalls, uint32_t drawCalls2d, uint32_t quads,
        uint32_t uiLayouts
    );
    void collectExtensionStatistics();                   // the statistics sections of the editor parts
    void updateProfilerPanel();                          // rereads the snapshot of Foundation::Profiler
    void setStripStatusText(const std::string &text);    // RAM / VRAM / CPU / GPU in the visibility strip

    // ---- The viewports: the engine pushes the output textures in and reads the frames out
    // (EditorViewports: the Scene and the Game surfaces).
    void setViewportSceneTexture(PandaUI::TextureHandle texture, bool flip) {
        if (m_viewportView) { m_viewportView->setSceneTexture(texture, flip); }
    }
    void setGameViewportTexture(PandaUI::TextureHandle texture, bool flip) {
        if (m_gameView) { m_gameView->setTexture(texture, flip); }
    }
    PandaUI::Rect viewportSceneFrame() const {
        return m_viewportView ? m_viewportView->sceneWorldFrame() : PandaUI::Rect();
    }
    ViewportSurfaces viewportSurfaces(); // once a frame: it also follows the focus of the surfaces
    // What the Scene surface collected since the last frame for the editor camera, the gizmo and
    // the tools.
    SceneNavigation takeSceneNavigation() {
        return m_viewportView ? m_viewportView->takeSceneNavigation() : SceneNavigation{};
    }
    ScenePointer takeScenePointer() {
        return m_viewportView ? m_viewportView->takeScenePointer() : ScenePointer{};
    }
    void setGizmoVisible(bool visible) {
        if (m_viewportView) { m_viewportView->setGizmoVisible(visible); }
    }
    void setGizmoActive(bool active) {
        if (m_viewportView) { m_viewportView->setGizmoActive(active); }
    }
    void setViewportToolCapture(bool capture) { // a viewport tool takes the left button over
        if (m_viewportView) { m_viewportView->setToolCapture(capture); }
    }
    void setViewportHelperLines(std::vector<Panda::HelperLine> lines) {
        if (m_viewportView) { m_viewportView->setHelperLines(std::move(lines)); }
    }
    // Which viewport renders this frame: the other keeps its last frame and says so (a veil).
    void setLiveViewport(bool gameLive) {
        if (m_viewportView) { m_viewportView->setSceneLive(!gameLive); }
        if (m_gameView) { m_gameView->setLive(gameLive); }
    }
    // The Game tab: a caption over the frame (empty — none), the keyboard focus of the game.
    void setGameViewportMessage(const std::string &text) {
        if (m_gameView) { m_gameView->setMessage(text); }
    }
    bool isGameViewportFocused() const {
        return m_gameView && m_gameView->isFocused();
    }
    void focusGameView();  // the keyboard focus to the Game surface (a click on the game)
    void openGameView();   // opens (or activates) the Game tab and gives it the focus — Play, Window → Game
    void showSceneView();  // the tab of the active world comes forward and the Scene renders — Stop
    // The project is about to go: the tabs of its entities and assets close, the history of the
    // selection is forgotten, the panels show that nothing is selected.
    void projectWillClose();
    // What the entity tabs show changed without the world changing (the script build was
    // loaded): the forms read their targets again.
    void refreshEntityForms() {
        if (m_editorTabs) { m_editorTabs->refreshEntityForms(); }
    }

    // ---- What else the application tells the chrome.
    void thumbnailDidBecomeReady(Panda::AssetId assetId); // the cell of the asset in the Content Browser follows
    void refreshAssetPanels();                            // the Content Browser rereads the registry
    // The registries of the editor gained entries (the editor parts of a project's modules were
    // loaded): what is built from them is built again.
    void extensionsChanged();
    void setUpdateStatus(const UpdateIndicatorModel &model); // the editor updater → the indicator of the toolbar
    void settingsFileReloaded(Panda::SettingsScope scope);   // settings.json / project.json was reread: its page follows
    // The second window of the editor — the Settings or the export of the game, one at a time:
    // opening one closes the other. The layer tells when the user closed the window and when it
    // got or lost the keyboard.
    void showExportWindow();
    void closeAuxiliaryWindow();
    void auxiliaryWindowFocusChanged(bool focused);
    // The docking layout (kept in the editor session by the layer).
    Panda::DockLayoutState captureDockLayout() const;
    void applyDockLayout(const Panda::DockLayoutState &state);

    // ---- What the commands of the application and the MCP server ask.
    // A panel in front by its id ("hierarchy", "contentBrowser", "output", "cmake", "statistics",
    // "profiler"); false — no such panel.
    bool showPanel(const std::string &id);
    void openKeymap(); // the Keymap page of the Settings window
    // File → New: the entities go to the root of the open world, the assets into the folder the
    // Content Browser shows. The same lists as in the context menus of the two panels.
    CreateActions newEntityActions();
    CreateActions newAssetActions();

    // ---- The parts of the workspace, and what they ask of it.
    EntityActions &entities() {
        return *m_entityActions;
    }
    WorldTabs &worldTabs() {
        return *m_worldTabs;
    }
    PandaUI::Context *context() const; // where a dialog is shown; nullptr — the chrome is in no window yet
    Panda::path_t contentDirectory(); // the folder the Content Browser shows; empty — no project
    void openDocumentAsset(Panda::AssetId asset); // a world or a prefab in its tab
    PandaUI::Rect hierarchyFrame() const;
    // The selection (Editor::selection()) and, at once, the panels and the tabs that show it.
    void selectEntities(const std::vector<Panda::UUID> &ids);
    void selectAssets(const std::vector<Panda::AssetId> &ids);
    void deselectAll();
    // What is selected, or something about it (a name, a component), changed: the panels and the
    // tabs read it again.
    void selectionChanged();
    // The Transform fields of the tabs already show the transform of the selection (the edit was
    // made in them): the per-frame sync does not refresh them under the caret.
    void transformShown();
    // The file items of a context menu (a tab, an asset): the path, the external editor, the
    // file manager. Nothing is added for an empty path.
    void addFileItems(PandaUI::Menu &menu, const std::filesystem::path &file);

    // Editor::EditorHost — the host of the editor parts of packages.
    std::string assetTitle(Panda::AssetId id) override;
    Panda::CommandHistory &assetUndoStack(Panda::AssetId id) override;
    void clearAssetUndoStack(Panda::AssetId id) override;
    void selectAsset(Panda::AssetId id) override;
    void revealAsset(Panda::AssetId id) override;
    void revealEntity(Panda::UUID id) override;
    void selectEntity(Panda::UUID id) override;
    PandaUI::TextureHandle assetThumbnail(Panda::AssetId id) override;
    PandaUI::FontFamily assetFont(Panda::AssetId id) override;
    void pickAsset(
        PandaUI::Context *context, PandaUI::Point at, const std::string &kind, std::function<void(uint32_t)> onPick
    ) override;
    std::optional<Panda::path_t> openFileDialog() override;
    // The viewport editing mode (border + banner + Stop): the owner of the state is the
    // coordinator; document is the target of Undo from the viewport, closing its tab ends
    // the session; onStop is the exit from the mode (the banner's Stop button, closing the tab).
    void beginViewportEditing(
        const std::string &title, Panda::AssetId document, std::function<void()> onStop
    ) override;
    void endViewportEditing() override;
    void refreshFromWorld() override; // the panels and the tabs reread the world (a structural edit, an undo)
    void refreshAssetEditors(Panda::AssetId id) override;
    Panda::TypeContext &settings(Panda::SettingsScope scope) override;
    void commitSettings(Panda::SettingsScope scope, Foundation::Shared<Panda::WorldCommand> command) override;
    void openSettings(Panda::SettingsScope scope, const std::string &section = {}) override;
    void restartEditor() override;
    void openInExternalEditor(const Panda::path_t &path, int line = 0, int column = 0) override;
    Panda::path_t scriptSource(const std::string &className) override;

    // ViewportPanelOutput.
    void viewportDidRectSelect(
        PandaUI::Point start, PandaUI::Point current, bool additive, RectSelectPhase phase
    ) override;
    void viewportDidPickGizmo(int mode) override;
    void viewportDidResetCamera() override;
    void viewportDidPickSpace(bool worldSpace) override;
    void viewportDidDropAssets(const std::vector<std::uint32_t> &assetIds, const std::string &kind) override;
    void viewportDidRequestDisplayOptions(PandaUI::View *anchor) override;
    void viewportDidToggleWorldSettings() override;
    void viewportDidRequestStopEditing() override;

    // ToolbarOutput.
    void toolbarDidTapPlay() override;
    void toolbarDidTapStop() override;
    void toolbarDidSave() override;
    void toolbarDidExport() override;
    void toolbarDidUndo() override;
    void toolbarDidRedo() override;
    void toolbarDidNavigateBack() override;
    void toolbarDidNavigateForward() override;
    void toolbarDidActivateUpdate() override;
    void toolbarDidDismissUpdate() override;

    // EditorTabsHost: what the tabs of the selection ask (EditorTabs.hpp).
    EntityEditorOutput &tabsHostOutput() override;
    void tabsBeginEntityTarget(const std::vector<Panda::UUID> &ids) override;
    void tabsEndEntityTarget() override;
    std::vector<Panda::Entity> tabsResolveEntities(const std::vector<Panda::UUID> &ids) override;
    Panda::World *tabsWorld() override;
    std::vector<std::string> tabsScriptClasses() override;
    void tabsAssetTabClosed(Panda::AssetId id) override;
    void tabsAssetTabOpened(Panda::AssetId id) override;
    void tabsDocumentClosed(Document *document) override;
    bool tabsSaveWorld() override;
    bool tabsSaveAsset(Panda::AssetId id) override;
    std::filesystem::path tabsWorldFilePath() override;
    void tabsApplySelection(
        const std::vector<Panda::UUID> &entities, const std::vector<Panda::AssetId> &assets
    ) override;
    void tabsHistoryChanged(bool canGoBack, bool canGoForward) override;

private:
    friend class GameDocument;
    void buildLayout();
    void registerDocumentTypes(); // the Plugins, UI Gallery and Game tabs in the "+" menu of a dock
    void registerCommands();
    // What the commands of the workspace do.
    void saveActiveDocument(); // Save: the document of the tab in focus
    void undo();               // on the stack of the active document (activeDocument)
    void redo();
    void openPlugins();   // the Plugins tab in the center dock
    void openUiGallery(); // the UI Gallery tab in the center dock
    void showCommandPalette();
    void showGoTo(); // the palette of the entities of the world and the assets of the project
    bool worldHasKeyboard() const; // the focus is in the viewport or the Hierarchy (or nowhere)
    int liveEntityCount() const; // the entities of the world the panels show, tombstones aside
    bool maximizeFocusedDock();
    bool closeFocusedTab();
    // The document that undo/redo is addressed to (the document model): the selected document tab
    // of the dock with the focus, the document of the active viewport session (brush strokes go
    // into its stack), else the world.
    Document *activeDocument();
    Document *m_lastUndoDocument = nullptr; // survives the focus loss caused by form rebuilds
    void showDisplayOptions(PandaUI::View *anchor); // the viewport section of the editor scope in a popover
    void setWorldSettingsVisible(bool visible);
    void refreshStatistics();                   // entity count → the statistics panel

    // The host of the editor parts (Editor::editorHost()) is this coordinator. The first member: it is set
    // BEFORE the panels are built, otherwise an AssetWell with a thumbnail (Settings → Environment Map)
    // would access the host before it exists.
    struct HostBinding final {
        explicit HostBinding(Editor::EditorHost *host) {
            Editor::setEditorHost(host);
        }
        ~HostBinding() {
            Editor::setEditorHost(nullptr);
        }
    };
    HostBinding m_hostBinding;
    Panda::EditorLayer &m_app; // the application: its documents, its viewports, Save / Play / builds
    // Before the panels: they are built with these as their outputs.
    std::unique_ptr<EntityActions> m_entityActions;
    std::unique_ptr<AssetActions> m_assetActions;
    Panda::World *m_panelsWorld = nullptr; // the world shown by the panels (editing or playing)
    uint64_t m_lastStructureVersion = 0;   // snapshot of World::getStructureVersion for syncWorldStructure
    std::uint64_t m_lastSelectionVersion = 0; // the version of the selection the panels show (syncSelection)
    std::uint64_t m_lastTransformVersion = 0; // transform version at the last push/edit (gizmo↔Inspector sync)
    // Cache of the toolbar enable states (init = the buttons' default enabled) — pushed to the View only
    // on change.
    bool m_tbCanUndo = true, m_tbCanRedo = true, m_tbCanPlay = true, m_tbCanStop = true;
    bool m_shownEditMode = true; // what the tabs were last told: editing, or the game plays
    std::shared_ptr<HierarchyPanelView> m_hierarchyView;
    std::shared_ptr<ContentBrowserView> m_contentView;
    std::shared_ptr<ToolbarPanelView> m_toolbarView;
    std::shared_ptr<OutputPanelView> m_outputView;
    std::shared_ptr<CMakePanelView> m_cmakeView;
    std::shared_ptr<StatisticsPanelView> m_statisticsView;
    std::shared_ptr<ProfilerPanelView> m_profilerView;
    std::shared_ptr<ViewportPanelView> m_viewportView;
    std::shared_ptr<GameViewportView> m_gameView; // the surface of the Game tab (one instance, the tab comes and goes)
    bool m_gameInFocus = false;                   // the Game is the viewport the user works in (ViewportSurfaces)
    // The selection changed under a selection gesture of the viewport: the tabs show it when the
    // gesture ends (a tab that opens next to the viewport resizes it under the rectangle).
    bool m_tabsAwaitGesture = false;
    std::shared_ptr<PandaUI::View> m_root;
    std::unique_ptr<DockManager> m_dockManager;
    std::unique_ptr<EditorTabs> m_editorTabs; // editor tabs of the selection (after m_dockManager)
    std::unique_ptr<WorldTabs> m_worldTabs;   // a tab per open world; the viewport lives in the tab of the active one
    // The settings sidebar of the viewport: the sections of the world scope over the context of
    // the panels' world.
    std::shared_ptr<SettingsSectionsView> m_worldSettingsView;
    // The active viewport editing session: null — no mode (the panel has no border);
    // the session document is the target of Undo from the viewport.
    std::function<void()> m_viewportEditingStop;
    Panda::AssetId m_viewportEditingDocument = 0;
    // The second window: the one that is open gives its place, then `show` opens the other.
    void replaceAuxiliaryWindow(std::function<void()> show);
    void showSettings(SettingsPage page, const std::string &section = {});

    std::unique_ptr<SettingsWindow> m_settings; // the Settings window: its pages and their documents
    std::unique_ptr<ExportWindow> m_export;     // the Export window
    Commands &m_commands; // the registry of the editor: the workspace adds its commands to it
};

} // namespace Editor
