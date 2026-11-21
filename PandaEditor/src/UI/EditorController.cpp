#include "UI/EditorController.hpp"

#include "App/EditorLayer.hpp"


#include "Preview/AssetPreviews.hpp"
#include "UI/Documents/CoreAssetEditors.hpp"
#include "UI/Documents/CoreComponentInspectors.hpp"
#include "UI/Documents/PluginsDocument.hpp"
#include "UI/ExportWindow.hpp"
#include "UI/SettingsWindow.hpp"
#include "UI/Documents/UiGalleryDocument.hpp"
#include "UI/AssetActions.hpp"
#include "UI/EditorDialogs.hpp"
#include "UI/EditorModelHelper.hpp"
#include "UI/EntityActions.hpp"
#include "UI/WorldTabs.hpp"
#include "UI/Settings/CoreSettingsSections.hpp"
#include <PandaEditorKit/Widgets/Icons.hpp>
#include <PandaEditorKit/Widgets/ObjectIcons.hpp>
#include <PandaEditorKit/Widgets/Picker.hpp>
#include "Project/Loader/AssetHandlerEditor.hpp"
#include "Project/System/SystemTools.hpp"

#include "PandaUI/Elements/TabView.hpp"
#include "PandaUI/Runtime/Context.hpp"

#include <Panda/Assets/FontAsset.hpp>
#include <Panda/GameLogic/Entity.hpp>
#include <Panda/GameLogic/GameContext.hpp>
#include <PandaEditorKit/Selection.hpp>
#include <Panda/GameLogic/World.hpp>
#include <Panda/ScriptEngine/ScriptEngine.hpp>
#include <Panda/WorldCommands/CommandHistory.hpp>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Editor {

using namespace PandaUI;

namespace {

    // The coordinator of the process: the document types of the registry are created through it
    // (the registry holds plain callables, the coordinator is created once).
    EditorController *s_controller = nullptr;

    constexpr const char *PLUGINS_DOCUMENT = "plugins";
    constexpr const char *UI_GALLERY_DOCUMENT = "uiGallery";
    constexpr const char *GAME_DOCUMENT = "game";
    constexpr const char *GAME_TAB_ID = "doc:game"; // EditorTabs names a document tab doc:<type>
    // A view is on screen when nothing up its chain is hidden (a tab that is not the selected one
    // of its dock is hidden by the TabView) and it belongs to a window.
    bool isViewOnScreen(const PandaUI::View *view) {
        for (const PandaUI::View *current = view; current != nullptr; current = current->getSuperview()) {
            if (current->isHidden()) { return false; }
            if (current->getSuperview() == nullptr) { return current->getContext() != nullptr; }
        }
        return false;
    }

} // namespace

// The Game document: the Game tab. The frame of the game, rendered by the engine into the Game
// surface; nothing to save or undo.
class GameDocument final : public Document {
public:
    explicit GameDocument(EditorController *controller)
        : m_controller(controller) {}

    std::string title() override {
        return "Game";
    }
    std::string icon() override {
        return Ic::Play;
    }
    std::shared_ptr<PandaUI::View> view() override {
        return m_controller->m_gameView;
    }

private:
    EditorController *m_controller;
};

/* ------------------------------ Coordinator ------------------------------ */

EditorController::EditorController(Panda::EditorLayer &app)
    : m_hostBinding(this)
    , m_app(app)
    , m_entityActions(std::make_unique<EntityActions>(*this, app))
    , m_assetActions(std::make_unique<AssetActions>(*this, app))
    , m_hierarchyView(std::make_shared<HierarchyPanelView>(m_entityActions.get()))
    , m_contentView(std::make_shared<ContentBrowserView>(m_assetActions.get()))
    , m_toolbarView(std::make_shared<ToolbarPanelView>(this))
    , m_outputView(std::make_shared<OutputPanelView>())
    , m_cmakeView(std::make_shared<CMakePanelView>(app.builds()))
    , m_statisticsView(std::make_shared<StatisticsPanelView>())
    , m_profilerView(std::make_shared<ProfilerPanelView>())
    , m_settings(std::make_unique<SettingsWindow>(app, app.commands()))
    , m_export(std::make_unique<ExportWindow>(app))
    , m_commands(app.commands()) {
    s_controller = this;
    // The core goes through the same registries as the modules (context/plugins.md, decision 2):
    // the settings sections, the asset editors, the component sections and the document types —
    // once per process, before the editor parts of the modules add theirs.
    static bool s_coreRegistered = false;
    if (!s_coreRegistered) {
        s_coreRegistered = true;
        registerCoreSettingsSections(editorExtensions());
        registerCoreAssetKinds(editorExtensions());
        registerCoreComponentInspectors(editorExtensions());
        registerDocumentTypes();
    }
    buildLayout();
    registerCommands();
    m_toolbarView->setKeyHints(m_commands);
    m_commands.addKeymapListener([] {
        if (s_controller == nullptr) { return; }
        s_controller->m_toolbarView->setKeyHints(s_controller->m_commands);
        s_controller->m_settings->keymapChanged();
    });
    selectionChanged(); // initial state: empty → status "-"
    refreshStatistics();
}


EditorController::~EditorController() {
    closeAuxiliaryWindow(); // the tree leaves the window before its documents go
    if (s_controller == this) { s_controller = nullptr; }
} // m_hostBinding unbinds the host last

void EditorController::closeAuxiliaryWindow() {
    if (m_settings->owns(m_lastUndoDocument)) { m_lastUndoDocument = nullptr; }
    m_settings->close();
    m_export->close();
}

void EditorController::auxiliaryWindowFocusChanged(bool focused) {
    m_settings->setFocused(focused);
    m_export->setFocused(focused);
}

// The second window shows one thing at a time. The one that is open gives its place from a tick
// of the main window: whoever asks may be a click or a key inside the window that goes.
void EditorController::replaceAuxiliaryWindow(std::function<void()> show) {
    if (!m_settings->isOpen() && !m_export->isOpen()) {
        show();
        return;
    }
    Context *context = m_root ? m_root->getContext() : nullptr;
    if (context == nullptr) { return; }
    context->postDeferred([this, show = std::move(show)] {
        closeAuxiliaryWindow();
        show();
    });
}

void EditorController::showExportWindow() {
    if (m_export->isOpen()) {
        m_export->show(); // comes to the front
        return;
    }
    replaceAuxiliaryWindow([this] { m_export->show(); });
}

void EditorController::showSettings(SettingsPage page, const std::string &section) {
    if (m_settings->isOpen()) {
        m_settings->show(page, section);
        return;
    }
    replaceAuxiliaryWindow([this, page, section] { m_settings->show(page, section); });
}

void EditorController::registerDocumentTypes() {
    // The Plugins tab: the store and the theme are the application's.
    DocumentType plugins;
    plugins.id = PLUGINS_DOCUMENT;
    plugins.title = "Plugins";
    plugins.icon = Ic::Plugins;
    plugins.create = [] {
        PluginsHost host;
        Panda::EditorLayer *app = &s_controller->m_app;
        host.store = &app->pluginStore();
        host.currentTheme = [app] { return app->appearance().currentThemeId(); };
        host.selectTheme = [app](const std::string &id) { app->appearance().selectTheme(id); };
        host.installDevPlugin = [app] {
            // The native folder dialog blocks: the store reports problems as a notice the tab shows.
            if (std::optional<Panda::path_t> folder = Panda::SystemTools::openFolderDialog()) {
                app->pluginStore().installDev(*folder);
            }
        };
        return std::unique_ptr<Document>(std::make_unique<PluginsDocument>(std::move(host)));
    };
    editorExtensions().documentTypes.add(std::move(plugins));

    // The UI Gallery tab (PandaUI/examples/gallery): the textures and the display face come from the
    // UI host of the application.
    DocumentType gallery;
    gallery.id = UI_GALLERY_DOCUMENT;
    gallery.title = "UI Gallery";
    gallery.create = [] {
        return std::unique_ptr<Document>(std::make_unique<UiGalleryDocument>(s_controller->m_app.ui().galleryHost()));
    };
    editorExtensions().documentTypes.add(std::move(gallery));

    // The Game tab: the frame of the game (EditorViewports). Opened by Play, Window → Game and the "+".
    DocumentType game;
    game.id = GAME_DOCUMENT;
    game.title = "Game";
    game.icon = Ic::Play;
    game.create = [] { return std::unique_ptr<Document>(std::make_unique<GameDocument>(s_controller)); };
    editorExtensions().documentTypes.add(std::move(game));
}

ViewportSurfaces EditorController::viewportSurfaces() {
    // The focus of a surface makes its viewport the one the user works in; it stays so while the
    // focus is elsewhere in the editor (a field of a form, the Hierarchy).
    if (m_gameView && m_gameView->isFocused()) {
        m_gameInFocus = true;
    } else if (m_viewportView && m_viewportView->sceneHasFocus()) {
        m_gameInFocus = false;
    }
    ViewportSurfaces surfaces;
    surfaces.gameInFocus = m_gameInFocus;
    if (m_viewportView) {
        surfaces.scene = m_viewportView->sceneWorldFrame();
        surfaces.sceneVisible = isViewOnScreen(m_viewportView.get()) && surfaces.scene.size.width >= 1.f &&
                                surfaces.scene.size.height >= 1.f;
    }
    if (m_gameView) {
        surfaces.game = m_gameView->getWorldFrame();
        surfaces.gameVisible =
            isViewOnScreen(m_gameView.get()) && surfaces.game.size.width >= 1.f && surfaces.game.size.height >= 1.f;
    }
    return surfaces;
}

void EditorController::openGameView() {
    if (!m_editorTabs || !m_dockManager) { return; }
    m_editorTabs->openDocumentType(GAME_DOCUMENT, Dock::Center); // an open tab is activated in its dock
    m_dockManager->showPanel(GAME_TAB_ID);                       // the focus: the keys go to the game
    m_gameInFocus = true;
}

bool EditorController::showPanel(const std::string &id) {
    if (!m_dockManager) { return false; }
    for (const char *known : {"hierarchy", "contentBrowser", "output", "cmake", "statistics", "profiler"}) {
        if (id == known) {
            m_dockManager->showPanel(id);
            return true;
        }
    }
    return false;
}

void EditorController::showSceneView() {
    if (m_worldTabs) { m_worldTabs->showActive(); }
    m_gameInFocus = false;
}

void EditorController::focusGameView() {
    if (!m_gameView || !m_root) { return; }
    if (Context *context = m_root->getContext()) { context->setFocusedView(m_gameView.get()); }
}

Panda::DockLayoutState EditorController::captureDockLayout() const {
    Panda::DockLayoutState state = m_dockManager->captureLayout();
    m_worldTabs->saveLayout(state); // which world is in front is the project session's to say
    state.worldSettingsVisible = m_viewportView && m_viewportView->isSidebarVisible();
    state.contentBrowserView = m_contentView->mode() == ContentBrowserView::Mode::List ? "list" : "icons";
    state.contentBrowserFolders = m_contentView->foldersShown();
    return state;
}

void EditorController::applyDockLayout(const Panda::DockLayoutState &state) {
    m_worldTabs->sync(); // the world tabs first: the pinned tabs of the center follow them
    m_dockManager->applyLayout(state);
    // Pinned editor tabs: restored after the layout (the world is already set —
    // EditorLayer calls setWorld before applyDockLayout; dead targets are skipped).
    if (m_editorTabs) { m_editorTabs->restorePinned(state.pinnedTabs); }
    if (state.isValid()) {
        // Every restored tab selected itself as it opened: the active tabs of the session go back on
        // top, and the keyboard returns to the dock it was in (the active document of undo).
        m_dockManager->applySelection(state);
        m_worldTabs->restoreLayout(state);
        if (state.focusedDock >= 0 && state.focusedDock <= 3) {
            m_dockManager->focusDock(static_cast<Dock>(state.focusedDock));
        }
    }
    setWorldSettingsVisible(state.worldSettingsVisible);
    m_contentView->setMode(state.contentBrowserView == "list" ? ContentBrowserView::Mode::List : ContentBrowserView::Mode::Icons);
    m_contentView->setFoldersShown(state.contentBrowserFolders);
}

void EditorController::setStripStatusText(const std::string &text) {
    if (m_dockManager) { m_dockManager->setStripStatusText(text); }
}
void EditorController::setWorld(Panda::World *world) {
    if (m_editorTabs) { m_editorTabs->closeAllEntityTabs(); } // the tab targets are in the previous world
    setActiveWorld(world);
    refreshAssetPanels(); // the project registry is available together with the world
}

void EditorController::setActiveWorld(Panda::World *world) {
    m_panelsWorld = world;
    // The settings sidebar edits this world's settings (in Play — the playing copy's).
    if (m_worldSettingsView) { m_worldSettingsView->setStore(world ? &world->context() : nullptr); }
    m_lastStructureVersion = world ? world->getStructureVersion() : 0;
    m_hierarchyView->setWorld(world); // rereads the tree from World
    // The forms of the entities follow the world: Play and Stop put another world under the open
    // tabs — the same ids, other entities. A form left on the entities of the world that went
    // shows what is no more and edits nothing.
    if (m_editorTabs) { m_editorTabs->refreshEntityForms(); }
    refreshStatistics();
    selectionChanged();
}

void EditorController::syncWorldStructure() {
    if (!m_panelsWorld) { return; }
    const uint64_t version = m_panelsWorld->getStructureVersion();
    if (version == m_lastStructureVersion) { return; }
    m_lastStructureVersion = version;
    // The selection is the editor's reaction to the world structure: deleted entities (destroy, clear,
    // scripts in Play) leave the selection here, not inside World.
    selection().forgetDeadEntities();
    m_hierarchyView->reload();
    refreshStatistics();
}

void EditorController::refreshFromWorld() {
    if (m_panelsWorld) { m_lastStructureVersion = m_panelsWorld->getStructureVersion(); }
    // An undo or a redo deleted, restored or moved entities: the selection has what is alive, and
    // its median (the gizmo) is where they are now.
    selection().forgetDeadEntities();
    selection().transformChanged();
    m_hierarchyView->reload();
    refreshStatistics();
    selectionChanged();
    // A loaded world, undo/redo, a pasted component: the values changed under the forms.
    if (m_editorTabs) { m_editorTabs->refreshEntityForms(); }
    if (m_worldSettingsView) { m_worldSettingsView->rebuild(); }
}

void EditorController::updateProfilerPanel() {
    if (m_profilerView) { m_profilerView->updateSnapshot(); }
}

void EditorController::setFrameStatistics(
    int fps, uint32_t drawCalls3d, uint32_t shadowDrawCalls, uint32_t drawCalls2d, uint32_t quads,
    uint32_t uiLayouts
) {
    if (m_statisticsView) {
        m_statisticsView->setFrameStatistics(fps, drawCalls3d, shadowDrawCalls, drawCalls2d, quads, uiLayouts);
    }
}

void EditorController::collectExtensionStatistics() {
    if (m_statisticsView) { m_statisticsView->collectExtensions(); }
}

void EditorController::extensionsChanged() {
    if (m_worldSettingsView) { m_worldSettingsView->rebuild(); }
    m_settings->storesChanged();
    if (m_statisticsView) { m_statisticsView->rebuildExtensionSections(); }
}

void EditorController::openInExternalEditor(const Panda::path_t &path, int line, int column) {
    m_app.openInExternalEditor(path, line, column);
}

Panda::path_t EditorController::scriptSource(const std::string &className) {
    return m_app.project().scriptSource(className);
}

void EditorController::restartEditor() {
    m_app.restartEditor();
}

void EditorController::saveActiveDocument() {
    // Cmd+S: the document of the tab with the focus (an entity tab and a side dock mean the world).
    if (Document *document = activeDocument()) { document->save(); }
}

void EditorController::addFileItems(Menu &menu, const std::filesystem::path &file) {
    if (file.empty()) { return; }
    menu.addItem("Copy Path", [file] { setClipboardText(file.string()); });
    // The path as the project names the file — in a world, in project.json, to a tool of the MCP
    // server: relative to the project folder, with forward slashes.
    if (Panda::AssetHandlerEditor *handler = Panda::AssetHandlerEditor::current()) {
        const std::filesystem::path project = handler->getProjectPath();
        std::error_code errorCode;
        const std::filesystem::path relative = std::filesystem::relative(file, project, errorCode);
        if (!project.empty() && !errorCode && !relative.empty() && *relative.begin() != "..") {
            const std::string inProject = relative.generic_string();
            menu.addItem("Copy Relative Path", [inProject] { setClipboardText(inProject); });
        }
    }
    menu.addItem("Open in External Editor", [this, file] {
        m_app.openInExternalEditor(file);
    });
    menu.addItem("Reveal in File Manager", [this, file] {
        m_app.revealInFileManager(file);
    });
}

void EditorController::settingsFileReloaded(Panda::SettingsScope scope) {
    m_settings->settingsFileReloaded(scope);
}

void EditorController::worldContextChanged() {
    m_lastUndoDocument = nullptr; // the undo target pointed into the previous context
    viewportDidRequestStopEditing(); // a viewport session edits what the previous world showed: the tool lets go
    if (!m_editorTabs) { return; }
    m_editorTabs->closeAllEntityTabs();
    m_editorTabs->clearHistory();
}

void EditorController::projectWillClose() {
    worldContextChanged();
    if (m_editorTabs) { m_editorTabs->closeAllAssetTabs(); }
    selectionChanged();
}

void EditorController::setWorldDisplayName(const std::string &name, const std::string &file, Panda::UUID prefabRoot) {
    if (m_hierarchyView) {
        m_hierarchyView->setRootTitle(name);
        m_hierarchyView->setRootEntity(prefabRoot);
    }
    if (m_dockManager) { m_dockManager->setPanelCaption("hierarchy", file); }
    if (m_viewportView) { m_viewportView->setWorldSettingsAvailable(prefabRoot == Panda::UUID(0)); }
}

void EditorController::thumbnailDidBecomeReady(Panda::AssetId assetId) {
    m_contentView->thumbnailDidBecomeReady(assetId);
}

void EditorController::refreshAssetPanels() {
    m_contentView->refresh();
}

/* ----------------------------- Selection flow ---------------------------- */

void EditorController::selectionChanged() {
    // The selection names only what exists: an asset whose file is gone leaves it here.
    selection().forgetDeadAssets();
    // The versions of what is shown: the per-frame sync (syncSelection) reacts to what changed since.
    m_lastSelectionVersion = selection().version();
    m_lastTransformVersion = selection().transformVersion();

    std::vector<Panda::Entity> entities = m_panelsWorld ? selection().entities() : std::vector<Panda::Entity>();
    if (!entities.empty()) {
        std::vector<Panda::UUID> ids;
        for (Panda::Entity &e : entities) { ids.push_back(e.getId()); }
        m_hierarchyView->setSelectedEntities(ids);
        // A single selection is found in the hierarchy: the ancestors open, the row scrolls into
        // view (a click on a visible row changes nothing). A multi-selection has no primary
        // entity to show — the set keeps no order — so it moves nothing.
        if (ids.size() == 1) { m_hierarchyView->revealEntity(ids.front()); }
        m_contentView->clearAssetSelection();
    } else {
        m_hierarchyView->setSelectedEntities({});
        // The assets were selected somewhere else than in the grid of the Content Browser (a
        // reference field, the history): its highlight is of another selection and goes.
        if (m_contentView->selectedAssets() != selection().assets()) { m_contentView->clearAssetSelection(); }
    }
    // Editor tabs: the selection opens/replaces the preview tab (the material preview is there too).
    // A selected world or prefab is shown by its own tab (WorldTabs::open), not by a tab of asset
    // parameters.
    std::vector<Panda::AssetId> shownAssets;
    if (entities.empty()) {
        auto *handler = Panda::AssetHandlerEditor::current();
        for (Panda::AssetId asset : selection().assets()) {
            if (handler == nullptr || handler->documentFilePath(asset).empty()) { shownAssets.push_back(asset); }
        }
    }
    // The tabs follow when the gesture that selects has ended: a tab opening in a dock next to
    // the viewport changes the size of the Scene, and the rest of the gesture would be read over
    // another picture than the one the press saw.
    m_tabsAwaitGesture = m_viewportView && m_viewportView->isSelecting();
    if (m_editorTabs && !m_tabsAwaitGesture) { m_editorTabs->showSelection(entities, shownAssets); }
}

void EditorController::selectEntities(const std::vector<Panda::UUID> &ids) {
    selection().selectEntities(ids);
    selectionChanged();
}

void EditorController::selectAssets(const std::vector<Panda::AssetId> &ids) {
    // An empty selection while it is already empty is not a change (a click on a FOLDER is filtered
    // to empty): the panels are not told, otherwise with an entity selected they would take the
    // highlight off the folder that has just got it.
    if (ids.empty() && selection().assets().empty()) { return; }
    selection().selectAssets(ids);
    selectionChanged();
}

void EditorController::transformShown() {
    m_lastTransformVersion = selection().transformVersion();
}

PandaUI::Context *EditorController::context() const {
    return m_root ? m_root->getContext() : nullptr;
}

Panda::path_t EditorController::contentDirectory() {
    return m_contentView ? m_contentView->currentDirectory() : Panda::path_t();
}

void EditorController::openDocumentAsset(Panda::AssetId asset) {
    if (m_worldTabs) { m_worldTabs->open(asset, /*preview*/ false); }
}

PandaUI::Rect EditorController::hierarchyFrame() const {
    return m_hierarchyView->getWorldFrame();
}

void EditorController::deselectAll() {
    selection().clear();
    m_contentView->clearAssetSelection(); // a highlighted folder too
    selectionChanged();
}

void EditorController::syncSelection() {
    if (!m_panelsWorld) { return; }
    // The viewport selected (a click, a rectangle) — or its gesture has ended and the tabs are due.
    const bool gestureEnded = m_tabsAwaitGesture && !(m_viewportView && m_viewportView->isSelecting());
    if (selection().version() != m_lastSelectionVersion || gestureEnded) {
        selectionChanged();
        return;
    }
    // Same selection, but the transform may have changed externally (gizmo drag) — update only the
    // Transform fields.
    const std::uint64_t transformVersion = selection().transformVersion();
    if (transformVersion != m_lastTransformVersion) {
        m_lastTransformVersion = transformVersion;
        if (m_editorTabs) { m_editorTabs->refreshTransforms(); }
    }
    if (m_editorTabs) { m_editorTabs->syncLifetimes(); } // tab targets died → the tabs are closed
}

void EditorController::updateChromeFromState(bool isEditMode) {
    if (m_settings->isCloseRequested() || m_export->isCloseRequested()) { closeAuxiliaryWindow(); }
    if (!m_toolbarView) { return; }
    m_worldTabs->sync();
    // The undo/redo buttons show the stack state of the ACTIVE document (just like Cmd+Z itself).
    Document *document = activeDocument();
    Panda::CommandHistory *stack = document ? document->undoStack() : nullptr;
    const bool canUndo = stack && stack->canUndo();
    const bool canRedo = stack && stack->canRedo();
    // Play is there whenever the world is edited: without the scripts of the project it explains
    // what is missing (toolbarDidTapPlay) instead of being a dead button.
    // Nothing to play without a world, and a prefab is not one.
    const bool canPlay = isEditMode && m_panelsWorld != nullptr && !m_app.isEditingPrefab();
    const bool canStop = !isEditMode;                  // Stop: only while the world is running

    if (canUndo != m_tbCanUndo) { m_tbCanUndo = canUndo; m_toolbarView->setCanUndo(canUndo); }
    if (canRedo != m_tbCanRedo) { m_tbCanRedo = canRedo; m_toolbarView->setCanRedo(canRedo); }
    if (canPlay != m_tbCanPlay) { m_tbCanPlay = canPlay; m_toolbarView->setCanPlay(canPlay); }
    if (canStop != m_tbCanStop) { m_tbCanStop = canStop; m_toolbarView->setCanStop(canStop); }
    if (isEditMode != m_shownEditMode) {
        m_shownEditMode = isEditMode;
        if (m_editorTabs) { m_editorTabs->setRuntimeMode(!isEditMode); } // "runtime world" banner in PLAY
    }

    // The "unsaved" dots: per tab, by isDirty of ITS document — a world tab by the change flag of
    // its world, an asset tab by exactly its assets (the document model: tabs are the source of
    // truth about what is unsaved).
    if (m_editorTabs) {
        m_editorTabs->syncDirtyStates();
        m_editorTabs->processPendingCloseRequests();
    }
}

/* ------------------------------- Layout ---------------------------------- */

void EditorController::buildLayout() {
    auto centerTabs = std::make_shared<TabView>();
    std::vector<DockPanel> panels;
    m_viewportView = std::make_shared<ViewportPanelView>(this);
    m_gameView = std::make_shared<GameViewportView>(); // the Game tab is a document (GameDocument) over this surface
    // The viewport is not a panel of its own: every open world is a tab of the center
    // (WorldTabs), and the viewport lives in the tab of the active one.
    m_viewportView->layout().setFlexGrow(1.f);
    panels.push_back({"hierarchy", "Hierarchy", Ic::PanelHierarchy, m_hierarchyView, Dock::Left});
    // The Inspector panel is gone: the selection opens as transient tabs (EditorTabs) in the right dock.
    panels.push_back({"contentBrowser", "Content Browser", Ic::PanelContentBrowser, m_contentView, Dock::Bottom});
    panels.push_back({"output", "Output", Ic::PanelOutput, m_outputView, Dock::Bottom});
    panels.push_back({"cmake", "CMake", Ic::PanelCMake, m_cmakeView, Dock::Bottom});
    // Diagnostic panels do not occupy the visibility strip — they are added through the dock's "+".
    panels.push_back({"statistics", "Statistics", Ic::PanelStatistics, m_statisticsView, Dock::Bottom,
                      /*open*/ true, /*inStrip*/ false});
    panels.push_back({"profiler", "Profiler", Ic::PanelProfiler, m_profilerView, Dock::Bottom,
                      /*open*/ true, /*inStrip*/ false});

    // What a UI test finds the parts of the window by (PandaUI/Core/Accessibility.hpp): a panel
    // by its id, the same one `show` of the MCP server takes.
    for (const DockPanel &panel : panels) { panel.content->setAccessibilityId(panel.id); }
    m_toolbarView->setAccessibilityId("toolbar");
    m_viewportView->setAccessibilityId("viewport");
    m_gameView->setAccessibilityId("game");

    m_dockManager = std::make_unique<DockManager>(m_toolbarView, centerTabs, std::move(panels));
    m_editorTabs = std::make_unique<EditorTabs>(m_dockManager.get(), this);
    m_worldTabs = std::make_unique<WorldTabs>(m_app, *m_dockManager, *m_editorTabs, m_viewportView);
    // The file of a tab's document in its context menu.
    m_dockManager->setTabMenuExtender([this](const std::string &panelId, Menu &menu) {
        if (Document *document = m_editorTabs->documentForTab(panelId)) { addFileItems(menu, document->filePath()); }
    });
    // The world settings live in the viewport sidebar (the world document), not in a panel.
    m_worldSettingsView = std::make_shared<SettingsSectionsView>(Panda::SettingsScope::World);
    m_viewportView->setSidebarContent(m_worldSettingsView);
    // The "+" of every dock offers the documents of the registry (the settings of the project and
    // of the editor, the documents of modules).
    m_dockManager->setAddMenuItemsProvider([this] {
        std::vector<DockManager::AddMenuItem> items;
        // A world is the first thing to add: with none open it is what the empty center offers.
        items.push_back({"New World", [this](Dock) { m_commands.run("file.newWorld"); }});
        for (const DocumentType &type : editorExtensions().documentTypes.types()) {
            const std::string id = type.id;
            items.push_back({type.title, [this, id](Dock dock) { m_editorTabs->openDocumentType(id, dock); }});
        }
        return items;
    });
    m_root = m_dockManager->rootView();
}

/* ------------------------------ File → New ------------------------------ */

CreateActions EditorController::newEntityActions() {
    return m_entityActions->hierarchyCreateActions(Panda::UUID(0)); // nothing without a world
}

CreateActions EditorController::newAssetActions() {
    // The titles do not depend on the folder: the menu is built before the browser has one.
    if (!Panda::AssetHandlerEditor::current()) { return {}; }
    CreateActions actions =
        m_assetActions->contentCreateActions(m_contentView ? m_contentView->currentDirectory() : Panda::path_t());
    for (CreateAction &action : actions) {
        // The new asset appears in the folder the browser shows: the browser comes forward.
        action.run = [this, run = std::move(action.run)] {
            if (m_dockManager) { m_dockManager->showPanel("contentBrowser"); }
            run();
        };
    }
    return actions;
}

void EditorController::beginViewportEditing(
    const std::string &title, Panda::AssetId document, std::function<void()> onStop
) {
    m_viewportEditingStop = std::move(onStop);
    m_viewportEditingDocument = document;
    if (m_viewportView) { m_viewportView->setEditingSession(true, title); }
}

void EditorController::endViewportEditing() {
    m_viewportEditingStop = nullptr;
    m_viewportEditingDocument = 0;
    if (m_viewportView) { m_viewportView->setEditingSession(false, {}); }
}

void EditorController::viewportDidRequestStopEditing() {
    // A copy: onStop usually calls endViewportEditing, which nulls m_viewportEditingStop.
    if (const std::function<void()> stop = m_viewportEditingStop) { stop(); }
}

std::string EditorController::assetTitle(Panda::AssetId id) {
    for (const auto &[builtin, name] : builtinShaderOptions()) {
        if (builtin == id) { return name; }
    }
    Panda::AssetHandlerEditor *handler = Panda::AssetHandlerEditor::current();
    if (!handler) { return {}; }
    auto it = handler->getRegistry().find(id);
    if (it == handler->getRegistry().end()) { return {}; }
    return handler->getAssetName(it->first);
}

bool EditorController::isBuiltinAsset(Panda::AssetId id) {
    const auto builtins = builtinShaderOptions();
    return id != Panda::AssetId(0) &&
           std::any_of(builtins.begin(), builtins.end(), [id](const auto &builtin) { return builtin.first == id; });
}

Panda::CommandHistory &EditorController::assetUndoStack(Panda::AssetId id) {
    if (auto *handler = Panda::AssetHandlerEditor::current()) { return handler->documents().undoStackFor(id); }
    static Panda::CommandHistory orphan; // no project — the command has nowhere to go (no forms without a project)
    return orphan;
}

void EditorController::clearAssetUndoStack(Panda::AssetId id) {
    if (auto *handler = Panda::AssetHandlerEditor::current()) { handler->documents().clearUndoStack(id); }
}

void EditorController::selectAsset(Panda::AssetId id) {
    if (id == Panda::AssetId(0) || isBuiltinAsset(id)) { return; } // a built-in one has no form and no file
    selectAssets({id}); // opens or replaces the asset tab
    revealAsset(id);    // the selection is shown where the asset lives too
}

void EditorController::revealAsset(Panda::AssetId id) {
    if (id == Panda::AssetId(0) || isBuiltinAsset(id)) { return; }
    if (m_dockManager) { m_dockManager->showPanel("contentBrowser"); }
    m_contentView->revealAsset(id);
}

void EditorController::revealEntity(Panda::UUID id) {
    if (!id) { return; }
    if (m_dockManager) { m_dockManager->showPanel("hierarchy"); }
    m_hierarchyView->flashEntity(id);
}

void EditorController::selectEntity(Panda::UUID id) {
    if (id) { selectEntities({id}); } // the Hierarchy reveals a single selection itself
}

PandaUI::TextureHandle EditorController::assetThumbnail(Panda::AssetId id) {
    auto *thumbnails = Panda::AssetPreviews::shared();
    return thumbnails ? thumbnails->thumbnailFor(id) : PandaUI::TextureHandle{};
}

// The face of a font asset for the editor's own UI (the preview of the font tab): rasterized once
// from the bytes of the asset; Sans when it cannot be read.
PandaUI::FontFamily EditorController::assetFont(Panda::AssetId id) {
    Panda::AssetRef<Panda::FontAsset> ref{id};
    const Panda::FontAsset *font = ref.get();
    if (!font || font->isEmpty()) { return PandaUI::FontFamily::Sans; }
    return m_app.ui().fontFamilyForAsset(id, font->data());
}

void EditorController::pickAsset(
    PandaUI::Context *context, PandaUI::Point at, const std::string &kind, std::function<void(uint32_t)> onPick
) {
    showAssetPicker(context, at, kind, std::move(onPick));
}

std::optional<Panda::path_t> EditorController::openFileDialog() {
    return Panda::SystemTools::openFileDialog();
}

void EditorController::refreshAssetEditors(Panda::AssetId id) {
    if (m_editorTabs) { m_editorTabs->refreshTabsForAsset(id); }
}

/* ---------------------------- Viewport output ---------------------------- */

void EditorController::viewportDidRectSelect(
    PandaUI::Point start, PandaUI::Point current, bool additive, RectSelectPhase phase
) {
    m_app.rectSelect(start, current, additive, phase);
}

void EditorController::viewportDidPickGizmo(int mode) {
    m_app.setGizmoOperation(mode);
}

void EditorController::viewportDidResetCamera() {
    m_app.resetCamera(); // camera → origin (through EditorLayer)
}

void EditorController::viewportDidPickSpace(bool worldSpace) {
    m_app.setGizmoSpace(worldSpace);
}

void EditorController::viewportDidDropAssets(const std::vector<std::uint32_t> &assetIds, const std::string &kind) {
    if (kind == "mesh") {
        std::vector<Panda::AssetId> ids;
        ids.reserve(assetIds.size());
        for (std::uint32_t id : assetIds) { ids.push_back(Panda::AssetId(id)); }
        m_entityActions->instantiateMeshes(ids);
        return;
    }
    // A prefab dropped onto the viewport becomes an instance in the world on screen.
    if (kind == "prefab") {
        std::vector<Panda::AssetId> ids;
        for (std::uint32_t id : assetIds) { ids.push_back(Panda::AssetId(id)); }
        m_entityActions->instantiatePrefabs(ids);
        return;
    }
    // A world dropped onto the viewport opens, as a double click on it in the Content Browser does.
    if (kind == "world" && !assetIds.empty()) { m_worldTabs->open(Panda::AssetId(assetIds.front()), /*preview*/ false); }
}

/* ----------------------------- Toolbar output ---------------------------- */

void EditorController::toolbarDidTapPlay() {
    // The game is the world plus the scripts of the project: without their build there is
    // nothing to play. The CMake panel is where they are built.
    Panda::ScriptEngine *engine = Panda::GameContext::getScriptEngine();
    if (engine == nullptr || !engine->isLoaded()) {
        showPanel("cmake");
        LOG_WARN_EDITOR(
            "Play needs the scripts of the project. Build them here in the CMake panel (Scripts, Build), then press Play."
        );
        if (Context *context = m_root ? m_root->getContext() : nullptr) {
            showConfirmDialog(
                context, "The scripts are not built",
                "The game is the world plus the C++ scripts of the project, and the scripts have not been built "
                "yet. The build runs in the CMake panel; the editor loads the scripts when it is done, and "
                "Play works from then on.",
                "Build Scripts", /*destructive*/ false,
                [this] {
                    m_app.builds().start(Panda::BuildTargetKind::Scripts, Panda::BuildAction::Build);
                }
            );
        }
        return;
    }
    m_app.play();
}

void EditorController::toolbarDidTapStop() {
    m_app.stop();
}

void EditorController::toolbarDidSave() {
    m_app.documentActions().saveAll(); // every unsaved file document
}

/* ------------------------------ Settings (the scopes) ------------------------------ */

void EditorController::viewportDidRequestDisplayOptions(View *anchor) {
    showDisplayOptions(anchor);
}

void EditorController::viewportDidToggleWorldSettings() {
    if (m_viewportView) { setWorldSettingsVisible(!m_viewportView->isSidebarVisible()); }
}

void EditorController::setWorldSettingsVisible(bool visible) {
    if (!m_viewportView) { return; }
    m_viewportView->setSidebarVisible(visible);
    if (visible && m_worldSettingsView) { m_worldSettingsView->rebuild(); } // a hidden sidebar is not kept fresh
}

// The display options of the viewport: the VIEWPORT section of the editor scope in a popover next
// to the gear (the same section the Editor Settings document shows).
void EditorController::showDisplayOptions(View *anchor) {
    Context *context = m_root ? m_root->getContext() : nullptr;
    if (!context || !anchor) { return; }
    auto sections = std::make_shared<SettingsSectionsView>(Panda::SettingsScope::Editor);
    sections->setTitleFilter([](const std::string &title) { return title == "VIEWPORT"; });
    sections->setStore(&settings(Panda::SettingsScope::Editor));
    auto card = std::make_shared<Panel>();
    applyElevation(*card, Elevation::Popover);
    card->layout().setSize({300.f, 230.f});
    card->layout().setFlexDirection(FlexDirection::Column);
    card->layout().setPadding(4.f);
    card->addSubview(sections);
    const Rect frame = anchor->getWorldFrame();
    auto popup = std::make_shared<Popup>(card, Rect(frame.origin.x, frame.origin.y + frame.size.height + 4.f, 0.f, 0.f));
    context->addOverlay(popup);
}

Panda::TypeContext &EditorController::settings(Panda::SettingsScope scope) {
    if (scope == Panda::SettingsScope::World) {
        PND_ASSERT(m_panelsWorld != nullptr, "WORLD SETTINGS WITHOUT A WORLD");
        return m_panelsWorld->context();
    }
    return m_app.settingsStore(scope);
}

void EditorController::commitSettings(Panda::SettingsScope scope, Foundation::Shared<Panda::WorldCommand> command) {
    if (!command) { return; }
    switch (scope) {
        case Panda::SettingsScope::World: {
            // An undo step of the world: the sidebar edits dirty the world like any edit.
            if (!m_panelsWorld) { return; }
            m_panelsWorld->getCommandHistory().execute(std::move(command));
            m_panelsWorld->setChanged();
            return;
        }
        case Panda::SettingsScope::Project: {
            // An undo step of the project settings page while the Settings window is open; an edit
            // made elsewhere (the start world chosen in the Export window) is applied as it is.
            // Either way project.json has an edit to save.
            if (!m_settings->commitProjectSettings(command)) {
                command->execute();
                m_app.documents().projectSettingsFile().markEdited();
            }
            return;
        }
        case Panda::SettingsScope::Editor: {
            // Applied and saved at once: no dirty state, no undo.
            command->execute();
            m_app.editorSettingsChanged();
            return;
        }
    }
}

void EditorController::openPlugins() {
    if (m_editorTabs) { m_editorTabs->openDocumentType(PLUGINS_DOCUMENT, Dock::Center); }
}

void EditorController::openKeymap() {
    showSettings(SettingsPage::Keymap);
}

void EditorController::openUiGallery() {
    if (m_editorTabs) { m_editorTabs->openDocumentType(UI_GALLERY_DOCUMENT, Dock::Center); }
}

void EditorController::openSettings(Panda::SettingsScope scope, const std::string &section) {
    switch (scope) {
        case Panda::SettingsScope::World:
            setWorldSettingsVisible(true);
            if (m_worldSettingsView && !section.empty()) { m_worldSettingsView->revealSection(section); }
            return;
        case Panda::SettingsScope::Project: showSettings(SettingsPage::Project, section); return;
        case Panda::SettingsScope::Editor: showSettings(SettingsPage::Editor, section); return;
    }
}

void EditorController::toolbarDidExport() {
    showExportWindow();
}

// Undo/redo go by the stack of the ACTIVE DOCUMENT (the document model): a document tab —
// its stack (an entity tab's is the world's), the viewport session — the document of the brush,
// otherwise the editing world. Called both by the toolbar and by the menu — the same semantics
// regardless of the UI.
Document *EditorController::activeDocument() {
    // The focus moves with every click (Context) — "where the user is" is reliable. The selected
    // document tab of the dock with the focus — its document; a side dock without one (Hierarchy)
    // — the world.
    // The Settings window has the keyboard: Save and Undo mean the document of its page.
    if (Document *document = m_settings->focusedDocument()) { return document; }
    Context *context = m_root ? m_root->getContext() : nullptr;
    View *focused = context ? context->getFocusedView() : nullptr;
    Dock dock;
    if (focused && m_dockManager && m_dockManager->dockContaining(focused, dock)) {
        const std::string panelId = m_dockManager->selectedPanelId(dock);
        if (Document *document = m_editorTabs && !panelId.empty() ? m_editorTabs->documentForTab(panelId) : nullptr) {
            m_lastUndoDocument = document;
            return document;
        }
        if (dock != Dock::Center) { // a side dock without a document tab — the world
            m_lastUndoDocument = m_worldTabs->activeDocument();
            return m_lastUndoDocument;
        }
    }
    // The viewport (or focus nowhere): a viewport session edits ITS OWN document — Cmd+Z undoes the stroke.
    if (m_viewportEditingDocument != Panda::AssetId(0) && m_editorTabs) {
        if (Document *document = m_editorTabs->documentForAsset(m_viewportEditingDocument)) {
            m_lastUndoDocument = document;
            return document;
        }
    }
    if (!focused) {
        // A form rebuild (undo/redo/refreshTab) destroys the view with the focus — Context nulls
        // it. The undo target did NOT change by that: the last one is held until the user clicks
        // somewhere else (a redo after an undo must hit the same stack).
        return m_lastUndoDocument ? m_lastUndoDocument : m_worldTabs->activeDocument();
    }
    m_lastUndoDocument = m_worldTabs->activeDocument();
    return m_lastUndoDocument;
}

void EditorController::undo() {
    Document *document = activeDocument();
    Panda::CommandHistory *stack = document ? document->undoStack() : nullptr;
    if (!stack || !stack->canUndo()) {
        LOG_WARN_EDITOR("Undo — nothing to undo");
        return;
    }
    stack->undo();
    // The world stack (the viewport, the entity tabs): the panels reread the world; a document of
    // its own: its form rereads the document.
    Document *world = m_worldTabs->activeDocument();
    if (world && m_panelsWorld && stack == &m_panelsWorld->getCommandHistory()) {
        world->refresh();
    } else {
        document->refresh();
    }
}

void EditorController::redo() {
    Document *document = activeDocument();
    Panda::CommandHistory *stack = document ? document->undoStack() : nullptr;
    if (!stack || !stack->canRedo()) {
        LOG_WARN_EDITOR("Redo — nothing to redo");
        return;
    }
    stack->redo();
    Document *world = m_worldTabs->activeDocument();
    if (world && m_panelsWorld && stack == &m_panelsWorld->getCommandHistory()) {
        world->refresh();
    } else {
        document->refresh();
    }
}

void EditorController::toolbarDidUndo() {
    undo();
}

void EditorController::toolbarDidRedo() {
    redo();
}

void EditorController::setUpdateStatus(const UpdateIndicatorModel &model) {
    if (m_toolbarView) { m_toolbarView->setUpdateStatus(model); }
}

void EditorController::toolbarDidActivateUpdate() {
    m_app.updateIndicatorDidActivate();
}

void EditorController::toolbarDidDismissUpdate() {
    m_app.updateIndicatorDidDismiss();
}

void EditorController::toolbarDidNavigateBack() {
    if (m_editorTabs) { m_editorTabs->goBack(); }
}

void EditorController::toolbarDidNavigateForward() {
    if (m_editorTabs) { m_editorTabs->goForward(); }
}

/* ----------------------------- Commands ---------------------------------- */

namespace {

    bool isInside(const View *view, const View *ancestor) {
        for (; view != nullptr; view = view->getSuperview()) {
            if (view == ancestor) { return true; }
        }
        return false;
    }

} // namespace

// The keys of the entity commands belong to the places that show the world — the Scene surface
// and the Hierarchy — and only while one of them has the keyboard. With the focus elsewhere (the
// Content Browser, a settings tab, the settings sidebar of the viewport) or nowhere, the same
// keys mean something else or nothing: Backspace there must not delete what is selected in the
// world.
bool EditorController::worldHasKeyboard() const {
    Context *context = m_root ? m_root->getContext() : nullptr;
    const View *focused = context ? context->getFocusedView() : nullptr;
    if (focused == nullptr) { return false; }
    return (m_viewportView && m_viewportView->sceneHasFocus()) || isInside(focused, m_hierarchyView.get());
}

void EditorController::registerCommands() {
    // The workspace is the open project's: without one its commands do not apply.
    auto command = [this](const char *id, const char *group, const char *label, std::function<bool()> run,
                          CommandScope scope = CommandScope::Editor) {
        m_commands.add(
            id, group, label, [this, run = std::move(run)] { return m_app.project().hasOpenedProject() && run(); }, scope
        );
    };
    // A command that applies whenever a project is open.
    auto action = [&command](const char *id, const char *group, const char *label, std::function<void()> run) {
        command(id, group, label, [run = std::move(run)] {
            run();
            return true;
        });
    };
    // An entity command: its key works only where the world has the keyboard.
    auto entityCommand = [&command](const char *id, const char *label, std::function<bool()> run) {
        command(id, "Entity", label, std::move(run), CommandScope::World);
    };
    m_commands.setWorldScopeTest([this] { return worldHasKeyboard(); });

    action("file.save", "File", "Save", [this] { saveActiveDocument(); });
    action("edit.undo", "Edit", "Undo", [this] { undo(); });
    action("edit.redo", "Edit", "Redo", [this] { redo(); });
    action("settings.open", "Settings", "Open Settings", [this] { openSettings(Panda::SettingsScope::Editor); });
    action("settings.openProject", "Settings", "Open Project Settings", [this] {
        openSettings(Panda::SettingsScope::Project);
    });
    action("keymap.open", "Settings", "Open Keymap", [this] { openKeymap(); });
    action("palette.show", "Editor", "Command Palette", [this] { showCommandPalette(); });
    action("goto.show", "Editor", "Go to Asset or Entity", [this] { showGoTo(); });
    // Find is of the panel the keyboard is in: the Content Browser takes it while it has the focus
    // (registered first, on the same key), the Hierarchy otherwise.
    command("contentBrowser.search", "Editor", "Find in Content Browser", [this] {
        if (!m_contentView->hasKeyboard()) { return false; }
        m_contentView->focusSearch();
        return true;
    });
    command("hierarchy.search", "Editor", "Find in Hierarchy", [this] {
        if (!showPanel("hierarchy")) { return false; }
        m_hierarchyView->focusSearch();
        return true;
    });
    command("tab.close", "Window", "Close Tab", [this] { return closeFocusedTab(); });
    command("dock.toggleFullscreen", "Window", "Toggle Fullscreen of the Panel",
            [this] { return maximizeFocusedDock(); });
    action("window.worldSettings", "Window", "World Settings", [this] { openSettings(Panda::SettingsScope::World); });
    action("window.game", "Window", "Game", [this] { openGameView(); });
    action("window.plugins", "Window", "Plugins", [this] { openPlugins(); });
    action("help.uiGallery", "Help", "UI Gallery", [this] { openUiGallery(); });
    action("camera.focusSelection", "View", "Focus Selection", [this] { m_app.focusSelection(); });
    action("camera.reset", "View", "Reset Camera", [this] { m_app.resetCamera(); });

    entityCommand("entity.delete", "Delete", [this] {
        const std::vector<Panda::UUID> ids = m_entityActions->targetIds();
        if (ids.empty()) { return false; }
        m_entityActions->hierarchyDidRequestDelete(ids);
        return true;
    });
    entityCommand("entity.duplicate", "Duplicate", [this] {
        const std::vector<Panda::UUID> ids = m_entityActions->targetIds();
        if (ids.empty()) { return false; }
        m_entityActions->hierarchyDidRequestDuplicate(ids);
        return true;
    });
    entityCommand("entity.copy", "Copy", [this] {
        const std::vector<Panda::UUID> ids = m_entityActions->targetIds();
        if (ids.empty()) { return false; }
        m_entityActions->hierarchyDidRequestCopy(ids);
        return true;
    });
    entityCommand("entity.cut", "Cut", [this] {
        const std::vector<Panda::UUID> ids = m_entityActions->targetIds();
        if (ids.empty()) { return false; }
        m_entityActions->hierarchyDidRequestCopy(ids); // the text goes to the clipboard, the entities go
        m_entityActions->hierarchyDidRequestDelete(ids);
        return true;
    });
    entityCommand("entity.paste", "Paste", [this] {
        if (!m_panelsWorld) { return false; }
        m_entityActions->hierarchyDidRequestPaste();
        return true;
    });
    entityCommand("entity.rename", "Rename", [this] {
        Panda::Entity entity = m_entityActions->primaryTarget();
        if (!entity.isValid()) { return false; }
        m_entityActions->hierarchyDidRequestRename(entity.getId());
        return true;
    });
    entityCommand("entity.selectAll", "Select All", [this] {
        if (!m_panelsWorld) { return false; }
        std::vector<Panda::UUID> ids;
        // Of a prefab — the entities of its tree: what else its world holds is not a part of it.
        const Panda::Entity root = m_entityActions->prefabRoot();
        for (auto handle : m_panelsWorld->getAllEntitiesWith<Panda::IdComponent>()) {
            Panda::Entity entity = m_panelsWorld->entity(handle);
            if (!m_panelsWorld->isValidEntt(handle)) { continue; }
            if (root.isValid() && !(entity == root) && !Panda::Entity(root).isAncestorOf(entity)) { continue; }
            ids.push_back(entity.getId());
        }
        if (ids.empty()) { return false; }
        selectEntities(ids);
        return true;
    });
    entityCommand("entity.deselect", "Deselect All", [this] {
        if (!selection().hasEntities()) { return false; }
        selectEntities({});
        return true;
    });

    // Prefabs (context/prefabs.md): what the menus of the Hierarchy and of the entity form do,
    // by name — for the palette, a key and an agent. They act on the selection: the first
    // selected entity, and for Revert and Apply the whole instance it is a part of.
    command("prefab.create", "Prefab", "Create Prefab from Selection", [this] {
        Panda::Entity entity = m_entityActions->primaryTarget();
        if (!entity.isValid()) { return false; }
        m_entityActions->createPrefab(entity.getId());
        return true;
    });
    command("prefab.unpack", "Prefab", "Unpack Prefab", [this] {
        const std::vector<Panda::UUID> ids = m_entityActions->targetIds();
        if (ids.empty()) { return false; }
        m_entityActions->unpackPrefabs(ids);
        return true;
    });
    command("prefab.open", "Prefab", "Open Prefab of Selection", [this] {
        Panda::Entity entity = m_entityActions->primaryTarget();
        if (!Panda::Prefabs::instanceRoot(entity).isValid()) { return false; }
        m_entityActions->openPrefabOf(entity.getId());
        return true;
    });
    // The instance of the selection: the one it is a part of, or the one it was added to.
    const auto selectedInstance = [this] {
        Panda::Entity primary = m_entityActions->primaryTarget();
        Panda::Entity root = Panda::Prefabs::instanceRoot(primary);
        return root.isValid() ? root : Panda::Prefabs::additionOf(primary).instance;
    };
    command("prefab.revert", "Prefab", "Revert Instance to Prefab", [this, selectedInstance] {
        Panda::Entity root = selectedInstance();
        if (!root.isValid()) { return false; }
        m_entityActions->revertPrefab(root.getId(), {});
        return true;
    });
    command("prefab.apply", "Prefab", "Apply Instance to Prefab", [this, selectedInstance] {
        Panda::Entity root = selectedInstance();
        if (!root.isValid()) { return false; }
        m_entityActions->applyPrefab(root.getId(), {}, /*forced*/ false);
        return true;
    });
    command("prefab.applyForced", "Prefab", "Apply Instance to Prefab, Forced", [this, selectedInstance] {
        Panda::Entity root = selectedInstance();
        if (!root.isValid()) { return false; }
        m_entityActions->applyPrefab(root.getId(), {}, /*forced*/ true);
        return true;
    });
    // The prefabs selected in the Content Browser, as instances in the world on screen.
    command("prefab.addToWorld", "Prefab", "Add Selected Prefabs to World", [this] {
        Panda::AssetHandlerEditor *handler = Panda::AssetHandlerEditor::current();
        std::vector<Panda::AssetId> prefabs;
        for (Panda::AssetId asset : selection().assets()) {
            if (handler != nullptr && handler->getInfo(asset).type == Panda::AssetType::PREFAB) { prefabs.push_back(asset); }
        }
        if (prefabs.empty() || m_panelsWorld == nullptr) { return false; }
        m_entityActions->instantiatePrefabs(prefabs);
        return true;
    });

    // The tools of the gizmo: the keys the viewport has always had, now with any panel in focus.
    static const char *const TOOLS[] = {"Move Tool", "Rotate Tool", "Scale Tool"};
    static const char *const TOOL_IDS[] = {"gizmo.move", "gizmo.rotate", "gizmo.scale"};
    for (int mode = 0; mode < 3; ++mode) {
        command(TOOL_IDS[mode], "Tools", TOOLS[mode], [this, mode] {
            if (!m_viewportView) { return false; }
            m_viewportView->setGizmoMode(mode);
            return true;
        });
    }

    action("world.play", "World", "Play", [this] { toolbarDidTapPlay(); });
    action("world.stop", "World", "Stop", [this] { toolbarDidTapStop(); });

    static const std::pair<const char *, const char *> PANELS[] = {
        {"hierarchy", "Hierarchy"},   {"contentBrowser", "Content Browser"}, {"output", "Output"},
        {"cmake", "CMake"},           {"statistics", "Statistics"},          {"profiler", "Profiler"},
    };
    for (const auto &[panelId, title] : PANELS) {
        const std::string panel = panelId;
        command(("panel." + panel).c_str(), "Window", (std::string("Show ") + title).c_str(), [this, panel] {
            return showPanel(panel);
        });
    }
}

// The command palette: every command of the editor by name with the keys that run it, and the
// lists of the menu bar (File → New, the recent projects). Enter or a click runs.
void EditorController::showCommandPalette() {
    Context *context = m_root ? m_root->getContext() : nullptr;
    if (context == nullptr) { return; }
    std::vector<PickerItem> items;
    // What choosing a row does, by the index of the row.
    auto actions = std::make_shared<std::vector<std::function<void()>>>();
    for (const Commands::Command &command : m_commands.all()) {
        if (command.id == "palette.show") { continue; }
        items.push_back({command.label, command.group, {}, m_commands.keys(command.id)});
        const std::string id = command.id;
        actions->push_back([this, id] {
            if (!m_commands.run(id)) {
                LOG_INFO_EDITOR("The command does not apply now: nothing to act on");
            }
        });
    }
    {
        for (const Panda::MenuCommandInfo &command : m_app.menuBar().dynamicItems()) {
            items.push_back({command.title, command.menu, {}, {}});
            const uint32_t id = command.id;
            actions->push_back([this, id] {
                m_app.menuBar().run(id);
            });
        }
    }
    showPalette(context, "Run a command…", std::move(items), [actions](int index) {
        if (index >= 0 && index < static_cast<int>(actions->size())) { (*actions)[static_cast<size_t>(index)](); }
    });
}

// Go to (⌘P): every entity of the world the panels show and every asset of the project by
// name. An entity becomes the selection, an asset is selected and shown where it lives.
void EditorController::showGoTo() {
    Context *context = m_root ? m_root->getContext() : nullptr;
    if (context == nullptr) { return; }
    std::vector<PickerItem> items;
    auto actions = std::make_shared<std::vector<std::function<void()>>>();
    if (m_panelsWorld) {
        std::vector<Panda::Entity> entities;
        m_panelsWorld->getAllEntitiesWith<Panda::IdComponent>().each([&](Panda::IdComponent &id) {
            Panda::Entity entity = m_panelsWorld->getById(id.id);
            if (entity.isValid()) { entities.push_back(entity); }
        });
        std::sort(entities.begin(), entities.end(), [](Panda::Entity &a, Panda::Entity &b) { return a.getName() < b.getName(); });
        for (Panda::Entity entity : entities) {
            Panda::Entity parent = entity.getParent();
            items.push_back({entity.getName(), parent.isValid() ? "in " + parent.getName() : "entity", entityIconName(entity), {}});
            const Panda::UUID id = entity.getId();
            actions->push_back([this, id] { selectEntity(id); });
        }
    }
    std::vector<uint32_t> assetIds;
    std::vector<PickerItem> assetItems = projectAssetItems(assetIds);
    for (size_t index = 0; index < assetItems.size(); index++) {
        items.push_back(std::move(assetItems[index]));
        const Panda::AssetId id = Panda::AssetId(assetIds[index]);
        actions->push_back([this, id] { selectAsset(id); });
    }
    showPalette(context, "Go to an asset or an entity…", std::move(items), [actions](int index) {
        if (index >= 0 && index < static_cast<int>(actions->size())) { (*actions)[static_cast<size_t>(index)](); }
    });
}

// The tab the keyboard is in closes — the selected tab of the dock that has the focus, or of
// the center when nothing has it — the way its close button closes it. The second window (the
// Settings, the Export), while it has the keyboard, is what closes.
bool EditorController::closeFocusedTab() {
    if (m_settings->isFocused()) {
        m_settings->requestClose();
        return true;
    }
    if (m_export->isFocused()) {
        m_export->requestClose();
        return true;
    }
    if (!m_dockManager) { return false; }
    Context *context = m_root ? m_root->getContext() : nullptr;
    View *focused = context ? context->getFocusedView() : nullptr;
    Dock dock = Dock::Center;
    if (focused != nullptr) { m_dockManager->dockContaining(focused, dock); }
    const std::string id = m_dockManager->selectedPanelId(dock);
    return !id.empty() && m_dockManager->closePanel(id);
}

bool EditorController::maximizeFocusedDock() {
    Context *context = m_root ? m_root->getContext() : nullptr;
    View *focused = context ? context->getFocusedView() : nullptr;
    if (!focused) { return false; }
    if (focused->isTextInput()) { return false; }
    Dock dock;
    if (!m_dockManager->dockContaining(focused, dock)) { return false; }
    m_dockManager->maximize(dock);
    return true;
}

/* ------------------------------ Statistics ------------------------------- */

// The entities of the world the panels show. An entity deleted by an undoable command stays in
// the registry as a tombstone until its command leaves the history: it is not counted.
int EditorController::liveEntityCount() const {
    int total = 0;
    // A prefab is the tree under its root: what else its world holds is not a part of it.
    if (Panda::Entity root = m_entityActions->prefabRoot(); root.isValid() && m_panelsWorld == root.getWorld()) {
        return static_cast<int>(m_panelsWorld->subtree(root).size());
    }
    if (m_panelsWorld) {
        for (auto handle : m_panelsWorld->getAllEntitiesWith<Panda::IdComponent>()) {
            if (m_panelsWorld->isValidEntt(handle)) { ++total; }
        }
    }
    return total;
}

void EditorController::refreshStatistics() {
    m_statisticsView->setEntityCount(liveEntityCount());
}

/* ------------------------------ EditorTabsHost ---------------------------- */

EntityEditorOutput &EditorController::tabsHostOutput() {
    return *m_entityActions;
}

void EditorController::tabsBeginEntityTarget(const std::vector<Panda::UUID> &ids) {
    m_entityActions->beginTarget(ids);
}

void EditorController::tabsEndEntityTarget() {
    m_entityActions->endTarget();
}

std::vector<Panda::Entity> EditorController::tabsResolveEntities(const std::vector<Panda::UUID> &ids) {
    std::vector<Panda::Entity> result;
    if (!m_panelsWorld) { return result; }
    for (Panda::UUID id : ids) {
        Panda::Entity e = m_panelsWorld->getById(id);
        if (e.isValid()) { result.push_back(e); }
    }
    return result;
}

std::vector<std::string> EditorController::tabsScriptClasses() {
    // Script classes for PickScript — the ScriptEngine manifest is read LIVE on open.
    std::vector<std::string> names;
    if (Panda::ScriptEngine *engine = Panda::GameContext::getScriptEngine(); engine && engine->isLoaded()) {
        for (const Panda::ScriptClassManifest &clazz : engine->getManifest().classes) {
            names.emplace_back(clazz.name);
        }
    }
    return names;
}

void EditorController::tabsAssetTabOpened(Panda::AssetId id) {
    if (auto *handler = Panda::AssetHandlerEditor::current()) { handler->setAssetOpenedInTab(id, true); }
}

void EditorController::tabsAssetTabClosed(Panda::AssetId id) {
    if (auto *handler = Panda::AssetHandlerEditor::current()) { handler->setAssetOpenedInTab(id, false); }
    // A viewport session without the tab of its document is an invisible LMB capture: the tool lets go.
    if (m_viewportEditingDocument == id) { viewportDidRequestStopEditing(); }
}

Panda::World *EditorController::tabsWorld() {
    return m_panelsWorld;
}

bool EditorController::tabsSaveWorld() {
    Document *world = m_worldTabs->activeDocument(); // an entity tab is a view of the active world
    return world && world->save();
}

bool EditorController::tabsSaveAsset(Panda::AssetId id) {
    return m_app.documentActions().saveAsset(id);
}

std::filesystem::path EditorController::tabsWorldFilePath() {
    Document *world = m_worldTabs->activeDocument();
    return world ? world->filePath() : std::filesystem::path();
}

void EditorController::tabsDocumentClosed(Document *document) {
    if (m_lastUndoDocument == document) { m_lastUndoDocument = nullptr; }
    m_worldTabs->documentClosed(document);
}

void EditorController::tabsApplySelection(
    const std::vector<Panda::UUID> &entities, const std::vector<Panda::AssetId> &assets
) {
    // The regular selection paths: Hierarchy/highlight/tab follow as on an ordinary click.
    if (!entities.empty()) {
        selectEntities(entities);
    } else if (!assets.empty()) {
        selectAssets(assets);
    }
}

void EditorController::tabsHistoryChanged(bool canGoBack, bool canGoForward) {
    if (m_toolbarView) { m_toolbarView->setCanNavigate(canGoBack, canGoForward); }
}

} // namespace Editor

