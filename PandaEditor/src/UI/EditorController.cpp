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
#include <Panda/Extensions/Extensions.hpp>
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
    m_viewportView->setKeyHints(m_commands);
    showTabToggles();
    m_commands.addKeymapListener([] {
        if (s_controller == nullptr) { return; }
        s_controller->m_toolbarView->setKeyHints(s_controller->m_commands);
        s_controller->m_viewportView->setKeyHints(s_controller->m_commands);
        s_controller->showTabToggles();
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
}

ViewportSurfaces EditorController::viewportSurfaces() {
    ViewportSurfaces surfaces;
    surfaces.scene = m_viewportView->sceneWorldFrame();
    surfaces.game = m_gameView->getWorldFrame();
    surfaces.showsGame = m_worldView->isShowingGame();
    // One of the two is in the middle of the world tab; it is on screen while that tab is in front.
    const PandaUI::View *shown = surfaces.showsGame ? static_cast<const PandaUI::View *>(m_gameView.get()) : m_viewportView.get();
    const PandaUI::Rect &frame = surfaces.showsGame ? surfaces.game : surfaces.scene;
    surfaces.visible = isViewOnScreen(shown) && frame.size.width >= 1.f && frame.size.height >= 1.f;
    return surfaces;
}

void EditorController::openGameView() {
    if (!m_worldTabs || !m_worldView) { return; }
    m_worldTabs->showActive();
    showView(true);
    focusGameView(); // the keys go to the game
}

EditorController::SidePanels EditorController::shownSidePanels() const {
    SidePanels panels;
    panels.left = m_dockManager->userVisible(Dock::Left);
    panels.right = m_dockManager->userVisible(Dock::Right);
    panels.bottom = m_dockManager->userVisible(Dock::Bottom);
    panels.tree = m_worldView->isTreeVisible();
    panels.inspector = m_worldView->isInspectorVisible();
    return panels;
}

void EditorController::showSidePanels(const SidePanels &panels) {
    m_dockManager->setVisible(Dock::Left, panels.left);
    m_dockManager->setVisible(Dock::Right, panels.right);
    m_dockManager->setVisible(Dock::Bottom, panels.bottom);
    m_worldView->setTreeVisible(panels.tree);
    m_worldView->setInspectorVisible(panels.inspector);
    m_dockManager->refreshStrip();
    m_dockManager->refreshTabToggles();
}

void EditorController::showView(bool game) {
    if (m_worldView->isShowingGame() == game) { return; }
    // The view that goes keeps the panels it has; the one that comes gets its own back.
    const SidePanels shown = shownSidePanels();
    if (game) {
        m_worldPanels = shown;
    } else {
        m_gamePanels = shown == m_worldPanels ? std::nullopt : std::optional<SidePanels>(shown);
    }
    m_worldView->showGame(game);
    if (!game) {
        showSidePanels(m_worldPanels);
    } else if (m_gamePanels) {
        showSidePanels(*m_gamePanels);
    }
}

void EditorController::showBuildBeforePlay() {
    // A maximized zone stays what the user made it: the build runs behind it (the strip says that
    // it runs), and its output comes forward only with something to say — an error.
    if (m_dockManager->isMaximized()) {
        m_buildPanelCame = false;
        return;
    }
    // Only a dock the build opened closes with it: one that was open stays, with the output in it.
    const bool open = m_dockManager->isDockOfPanelVisible("cmake");
    showPanel("cmake");
    m_buildPanelCame = !open;
}

void EditorController::buildBeforePlayEnded(bool built) {
    const bool came = std::exchange(m_buildPanelCame, false);
    if (!built) {
        showPanel("cmake"); // the errors, in front of whatever was maximized
    } else if (came) {
        m_dockManager->hideDockOfPanel("cmake");
    }
}

bool EditorController::showPanel(const std::string &id) {
    if (!m_dockManager) { return false; }
    // The parts of the world tab: the tab of the active world comes forward and shows the part.
    if (id == "hierarchy" || id == "inspector") {
        if (m_worldTabs) { m_worldTabs->showActive(); }
        if (id == "hierarchy") {
            setTreeVisible(true);
        } else {
            setInspectorVisible(true);
        }
        return true;
    }
    for (const char *known : {"contentBrowser", "output", "cmake", "statistics", "profiler"}) {
        if (id == known) {
            m_dockManager->showPanel(id);
            return true;
        }
    }
    return false;
}

void EditorController::setTreeVisible(bool visible) {
    if (!m_worldView || m_worldView->isTreeVisible() == visible) { return; }
    m_worldView->setTreeVisible(visible);
    if (m_dockManager) { m_dockManager->refreshTabToggles(); }
}

void EditorController::setInspectorVisible(bool visible) {
    if (!m_worldView || m_worldView->isInspectorVisible() == visible) { return; }
    m_worldView->setInspectorVisible(visible);
    if (m_dockManager) { m_dockManager->refreshTabToggles(); }
}

void EditorController::showTabToggles() {
    // The columns of the world tab, by where they are: the tree at the left, the inspector at
    // the right. In the bar of the tab — at the right of the tab bar, as the editor actions of a
    // code editor — while a world tab is in front; a material has no columns to hide.
    m_dockManager->setTabToggles(
        {
            {"showHierarchy", Ic::Sidebar, m_commands.hint("Hierarchy", "window.toggleHierarchy"),
             [this] { return m_worldView->isTreeVisible(); }, [this] { setTreeVisible(!m_worldView->isTreeVisible()); }},
            {"showInspector", Ic::SidebarRight, m_commands.hint("Inspector", "window.toggleInspector"),
             [this] { return m_worldView->isInspectorVisible(); },
             [this] { setInspectorVisible(!m_worldView->isInspectorVisible()); }},
        },
        [](const std::string &tabId) { return WorldTabs::isWorldTab(tabId); }
    );
}

void EditorController::showWorldView() {
    if (m_worldTabs) { m_worldTabs->showActive(); }
    if (m_worldView) { showView(false); }
}

bool EditorController::isGameUnderPointer(PandaUI::Point position) const {
    Context *context = m_root ? m_root->getContext() : nullptr;
    return context != nullptr && m_gameView && context->viewAt(position) == m_gameView.get();
}

void EditorController::focusGameView() {
    if (!m_gameView || !m_root) { return; }
    if (Context *context = m_root->getContext()) { context->setFocusedView(m_gameView.get()); }
}

Panda::DockLayoutState EditorController::captureDockLayout() const {
    Panda::DockLayoutState state = m_dockManager->captureLayout();
    m_worldTabs->saveLayout(state); // which world is in front is the project session's to say
    m_worldView->saveLayout(state);
    // The panels the layout names are the World view's — the editor opens with it; the Game's own
    // go beside them. While the Game shows, what is on screen is the Game's.
    state.gamePanels = m_gamePanels;
    if (m_worldView->isShowingGame()) {
        const SidePanels shown = shownSidePanels();
        state.gamePanels = shown == m_worldPanels ? std::nullopt : std::optional<SidePanels>(shown);
        state.leftVisible = m_worldPanels.left;
        state.rightVisible = m_worldPanels.right;
        state.bottomVisible = m_worldPanels.bottom;
        state.worldTreeVisible = m_worldPanels.tree;
        state.worldInspectorVisible = m_worldPanels.inspector;
    }
    state.contentBrowserView = m_contentView->mode() == ContentBrowserView::Mode::List ? "list" : "icons";
    state.contentBrowserFolders = m_contentView->foldersShown();
    state.contentBrowserRowHeight = m_contentView->listRowHeight();
    return state;
}

void EditorController::applyDockLayout(const Panda::DockLayoutState &saved) {
    // No layout yet, or one of another model of the window: the window opens with the defaults.
    const Panda::DockLayoutState state = saved.isValid() ? saved : Panda::DockLayoutState{};
    m_worldTabs->sync(); // the world tabs first: the other tabs of the center follow them
    m_dockManager->applyLayout(state);
    // The tabs of documents: restored after the layout (the project is loaded — EditorLayer calls
    // setWorld before applyDockLayout; an asset that is gone is skipped).
    if (m_editorTabs) { m_editorTabs->restoreTabs(state.openTabs); }
    m_worldView->applyLayout(state);
    if (state.isValid()) {
        // Every restored tab selected itself as it opened: the active tabs of the session go back on
        // top, and the keyboard returns to the dock it was in (the active document of undo).
        m_dockManager->applySelection(state);
        m_worldTabs->restoreLayout(state);
        if (state.focusedDock >= 0 && state.focusedDock <= 3) {
            m_dockManager->focusDock(static_cast<Dock>(state.focusedDock));
        }
    }
    // The files of the project are a column at the left: a list, the project panel of VS Code and
    // Zed, unless the layout says icons.
    m_contentView->setMode(
        state.contentBrowserView == "icons" ? ContentBrowserView::Mode::Icons : ContentBrowserView::Mode::List
    );
    m_contentView->setFoldersShown(state.contentBrowserFolders);
    m_contentView->setListRowHeight(state.contentBrowserRowHeight); // 0 — the default
    // The panels just applied are the World view's; the Game's own wait for the game.
    m_gamePanels = state.gamePanels;
    if (m_worldView->isShowingGame()) {
        m_worldPanels = shownSidePanels();
        if (m_gamePanels) { showSidePanels(*m_gamePanels); }
    }
    m_dockManager->refreshStrip();
    m_dockManager->refreshTabToggles(); // the buttons of the tree and the inspector say what is shown
}

void EditorController::setStripStatusText(const std::string &text) {
    if (m_dockManager) { m_dockManager->setStripStatusText(text); }
}
void EditorController::setWorld(Panda::World *world) {
    setActiveWorld(world);
    refreshAssetPanels(); // the project registry is available together with the world
}

void EditorController::setActiveWorld(Panda::World *world) {
    m_panelsWorld = world;
    m_lastStructureVersion = world ? world->getStructureVersion() : 0;
    m_hierarchyView->setWorld(world); // rereads the tree from World
    refreshStatistics();
    // The inspector follows the world: Play and Stop put another world under it — the same ids,
    // other entities. A form left on the entities of the world that went shows what is no more
    // and edits nothing.
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
    // A loaded world, undo/redo, a pasted component: the values changed under the inspector.
    refreshInspector();
}

void EditorController::refreshInspector() {
    if (!m_inspectorView) { return; }
    const std::vector<Panda::Entity> entities = m_panelsWorld ? selection().entities() : std::vector<Panda::Entity>();
    m_inspectorEntities.clear();
    for (const Panda::Entity &entity : entities) { m_inspectorEntities.push_back(entity.getId()); }
    m_inspectorWorld = m_panelsWorld;
    m_inspectorView->setEntities(entities);
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
    m_settings->storesChanged();
    refreshInspector(); // the sections of the components of the modules
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
    // Cmd+S: the document of the tab with the focus (a side dock means the world in front).
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
    clearSelectionHistory();
}

void EditorController::worldWillDeactivate() {
    if (!m_worldTabs) { return; }
    WorldTabState state;
    state.tree = m_hierarchyView->captureState();
    state.selection.assign(selection().entityIds().begin(), selection().entityIds().end());
    m_worldTabs->keepActiveState(std::move(state));
}

void EditorController::worldDidActivate() {
    if (!m_worldTabs) { return; }
    // What the tab showed of its world when it was left: the branches open again, the selection
    // comes back (the entities that are gone are not in it).
    const WorldTabState kept = m_worldTabs->takeActiveState();
    m_hierarchyView->restoreState(kept.tree);
    if (!kept.selection.empty()) { selectEntities(kept.selection); }
}

void EditorController::projectWillClose() {
    worldContextChanged();
    if (m_editorTabs) { m_editorTabs->closeAllAssetTabs(); }
    selectionChanged();
}

void EditorController::setWorldDisplayName(const std::string &name, Panda::UUID prefabRoot) {
    if (m_hierarchyView) {
        m_hierarchyView->setRootTitle(name);
        m_hierarchyView->setRootEntity(prefabRoot);
    }
}

void EditorController::thumbnailDidBecomeReady(Panda::AssetId assetId) {
    m_contentView->thumbnailDidBecomeReady(assetId);
}

void EditorController::refreshAssetPanels() {
    m_contentView->refresh();
    // A project that has just opened: the Content Browser shows the folder it showed at the end of
    // the last session of the project (ProjectSession::contentBrowserFolder).
    const Panda::path_t project = m_app.project().getOpenedProjectPath();
    if (project != m_contentProject) {
        m_contentProject = project;
        const std::string &folder = m_app.project().projectSession().contentBrowserFolder;
        if (!project.empty() && !folder.empty()) { m_contentView->showFolder(project / folder); }
    }
}

/* ----------------------------- Selection flow ---------------------------- */

void EditorController::selectionChanged() {
    // The selection names only what exists: an asset whose file is gone leaves it here.
    selection().forgetDeadAssets();
    // The versions of what is shown: the per-frame sync (syncSelection) reacts to what changed since.
    m_lastSelectionVersion = selection().version();
    m_lastTransformVersion = selection().transformVersion();

    // The entities: the tree, the inspector and the history of the world tab.
    std::vector<Panda::Entity> entities = m_panelsWorld ? selection().entities() : std::vector<Panda::Entity>();
    std::vector<Panda::UUID> ids;
    for (Panda::Entity &e : entities) { ids.push_back(e.getId()); }
    m_hierarchyView->setSelectedEntities(ids);
    // A single selection is found in the hierarchy: the ancestors open, the row scrolls into view
    // (a click on a visible row changes nothing). A multi-selection has no primary entity to show
    // — the set keeps no order — so it moves nothing.
    if (ids.size() == 1) { m_hierarchyView->revealEntity(ids.front()); }
    // The form is built again for other entities only: a file selected in the browser leaves it as
    // it is — where it is scrolled, the field with the caret.
    if (ids != m_inspectorEntities || m_panelsWorld != m_inspectorWorld) { refreshInspector(); }
    if (!ids.empty()) { recordSelectionStep(ids); }
    // The assets selected anywhere (a click in the browser, an import, a new asset) are the ones
    // the Content Browser highlights where it shows them.
    if (m_contentView->selectedAssets() != selection().assets()) { m_contentView->showSelectedAssets(selection().assets()); }
}

void EditorController::selectEntities(const std::vector<Panda::UUID> &ids) {
    selection().selectEntities(ids);
    selectionChanged();
}

void EditorController::selectAssets(const std::vector<Panda::AssetId> &ids) {
    // An empty selection while it is already empty is not a change (a click on a FOLDER is filtered
    // to empty): the panels are not told, otherwise they would take the highlight off the folder
    // that has just got it.
    if (ids.empty() && selection().assets().empty()) { return; }
    selection().selectAssets(ids);
    selectionChanged();
}

/* ----------------------------- Selection history ---------------------------- */

void EditorController::recordSelectionStep(const std::vector<Panda::UUID> &entities) {
    if (m_navigatingSelection) { return; } // Back / Forward do not record themselves again
    std::vector<Panda::UUID> step = entities;
    std::sort(step.begin(), step.end());
    if (m_selectionCursor >= 0 && m_selectionHistory[static_cast<size_t>(m_selectionCursor)] == step) { return; }
    constexpr size_t HISTORY_CAP = 50;
    m_selectionHistory.resize(static_cast<size_t>(m_selectionCursor + 1)); // a new selection cuts the forward branch off
    m_selectionHistory.push_back(std::move(step));
    if (m_selectionHistory.size() > HISTORY_CAP) { m_selectionHistory.erase(m_selectionHistory.begin()); }
    m_selectionCursor = static_cast<int>(m_selectionHistory.size()) - 1;
    showSelectionHistory();
}

void EditorController::navigateSelection(int step) {
    // The entries whose entities are all gone are passed over.
    for (int cursor = m_selectionCursor + step; cursor >= 0 && cursor < static_cast<int>(m_selectionHistory.size());
         cursor += step) {
        std::vector<Panda::UUID> alive;
        for (Panda::UUID id : m_selectionHistory[static_cast<size_t>(cursor)]) {
            if (m_panelsWorld && m_panelsWorld->isValid(id)) { alive.push_back(id); }
        }
        if (alive.empty()) { continue; }
        m_selectionCursor = cursor;
        m_navigatingSelection = true;
        selectEntities(alive);
        m_navigatingSelection = false;
        showSelectionHistory();
        return;
    }
}

void EditorController::clearSelectionHistory() {
    m_selectionHistory.clear();
    m_selectionCursor = -1;
    showSelectionHistory();
}

void EditorController::showSelectionHistory() {
    if (!m_toolbarView) { return; }
    m_toolbarView->setCanNavigate(
        m_selectionCursor > 0, m_selectionCursor + 1 < static_cast<int>(m_selectionHistory.size())
    );
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

PandaUI::Rect EditorController::hierarchyFrame() const {
    return m_hierarchyView->getWorldFrame();
}

void EditorController::syncSelection() {
    if (m_editorTabs) { m_editorTabs->syncLifetimes(); } // the asset of a tab is gone → the tab closes
    if (!m_panelsWorld) { return; }
    // The viewport selected (a click, a rectangle), the game destroyed a selected entity.
    if (selection().version() != m_lastSelectionVersion) {
        selectionChanged();
        return;
    }
    // Same selection, but the transform may have changed externally (gizmo drag) — update only the
    // Transform fields.
    const std::uint64_t transformVersion = selection().transformVersion();
    if (transformVersion != m_lastTransformVersion) {
        m_lastTransformVersion = transformVersion;
        if (m_inspectorView) { m_inspectorView->refreshTransform(); }
    }
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
    const bool showsGame = m_worldView->isShowingGame();
    const bool canSwitch = m_panelsWorld != nullptr;
    if (showsGame != m_tbShowsGame || canSwitch != m_tbCanSwitchView) {
        m_tbShowsGame = showsGame;
        m_tbCanSwitchView = canSwitch;
        m_toolbarView->setShownView(showsGame, canSwitch);
    }
    if (isEditMode != m_shownEditMode) {
        m_shownEditMode = isEditMode;
        if (m_inspectorView) { m_inspectorView->setRuntimeMode(!isEditMode); } // "runtime world" banner in PLAY
    }

    // The "unsaved" dots: per tab, by isDirty of ITS document — a world tab by the change flag of
    // its world, an asset tab by its asset (the document model: tabs are the source of truth
    // about what is unsaved).
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
    m_gameView = std::make_shared<GameViewportView>(); // the Game: the middle of the world tab in its place of the Scene
    // The tree, the Scene and the inspector are not panels of their own: every open world is a tab
    // of the center (WorldTabs), and what a world tab shows lives in the tab of the active one.
    m_viewportView->layout().setFlexGrow(1.f);
    m_inspectorView = std::make_shared<EntityEditorView>(m_entityActions.get());
    m_inspectorView->setScriptClassProvider([this] {
        // The classes the picker offers: the manifest of the loaded script build, read when it opens.
        std::vector<std::string> names;
        if (Panda::ScriptEngine *engine = Panda::GameContext::getScriptEngine(); engine && engine->isLoaded()) {
            for (const Panda::ScriptClassManifest &clazz : engine->getManifest().classes) { names.emplace_back(clazz.name); }
        }
        return names;
    });
    m_worldView = std::make_shared<WorldEditorView>(m_hierarchyView, m_viewportView, m_gameView, m_inspectorView);
    // The files of the project are the left column of the window, the whole height of it.
    panels.push_back({"contentBrowser", "Content Browser", Ic::PanelContentBrowser, m_contentView, Dock::Left});
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
    m_hierarchyView->setAccessibilityId("hierarchy");
    m_viewportView->setAccessibilityId("viewport");
    m_inspectorView->setAccessibilityId("inspector");
    m_worldView->setAccessibilityId("worldEditor");
    m_gameView->setAccessibilityId("game");

    m_dockManager = std::make_unique<DockManager>(m_toolbarView, centerTabs, std::move(panels));
    // The output of the builds waits under the center until it is asked for: a build shows it, and
    // so do its button in the strip and the command of the Window menu.
    m_dockManager->setVisible(Dock::Bottom, false);
    m_editorTabs = std::make_unique<EditorTabs>(m_dockManager.get(), this);
    m_worldTabs = std::make_unique<WorldTabs>(m_app, *m_dockManager, *m_editorTabs, m_worldView);
    // The file of a tab's document in its context menu; a file that is an asset of the project
    // has a place in the Content Browser too.
    m_dockManager->setTabMenuExtender([this](const std::string &panelId, Menu &menu) {
        Document *document = m_editorTabs->documentForTab(panelId);
        if (document == nullptr) { return; }
        const std::filesystem::path file = document->filePath();
        if (file.empty()) { return; }
        addFileItems(menu, file);
        Panda::AssetHandlerEditor *handler = Panda::AssetHandlerEditor::current();
        const Panda::AssetId asset = handler != nullptr ? handler->getAssetId(file) : Panda::AssetId(0);
        if (asset) {
            menu.addItem("Reveal in Content Browser", [this, asset] { revealAsset(asset); });
        }
    });
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

void EditorController::openAsset(Panda::AssetId id) {
    if (id == Panda::AssetId(0) || isBuiltinAsset(id)) { return; } // a built-in one has no form and no file
    Panda::AssetHandlerEditor *handler = Panda::AssetHandlerEditor::current();
    if (handler == nullptr || !handler->hasAsset(id)) { return; }
    revealAsset(id);    // where the asset lives (going into its folder clears the grid)
    selectAssets({id}); // and its cell is highlighted
    // A world and a prefab are documents of the center; an asset with a form has a tab of its own;
    // a file the editor has no form for goes to the external editor of the settings.
    if (!handler->documentFilePath(id).empty()) {
        m_worldTabs->open(id);
        return;
    }
    const AssetKindUi *kind = editorExtensions().assetKinds.find(assetTypeString(handler->getInfo(id).type));
    if (kind != nullptr && kind->createEditor != nullptr) {
        m_editorTabs->openAsset(id);
        return;
    }
    const Panda::path_t relative = handler->assetPath(id);
    if (!relative.empty()) { openInExternalEditor(handler->getProjectPath() / relative); }
}

void EditorController::importFiles(const std::vector<Panda::path_t> &files) {
    if (m_assetActions) { m_assetActions->contentDidRequestImportFiles(files, m_contentView->currentDirectory()); }
}

void EditorController::revealAsset(Panda::AssetId id) {
    if (id == Panda::AssetId(0) || isBuiltinAsset(id)) { return; }
    if (m_dockManager) { m_dockManager->showPanel("contentBrowser"); }
    m_contentView->revealAsset(id);
}

void EditorController::revealEntity(Panda::UUID id) {
    if (!id) { return; }
    showPanel("hierarchy");
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
    if (kind == "world" && !assetIds.empty()) { m_worldTabs->open(Panda::AssetId(assetIds.front())); }
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
    if (m_app.buildBeforePlay(/*ask*/ true)) { return; } // the scripts changed: Play follows their build
    m_app.play();
}

void EditorController::toolbarDidTapStop() {
    m_app.stop();
}

void EditorController::toolbarDidSelectView(bool game) {
    if (game) {
        openGameView();
    } else {
        showWorldView();
    }
}

void EditorController::toolbarDidSave() {
    m_app.documentActions().saveAll(); // every unsaved file document
}

/* ------------------------------ Settings (the scopes) ------------------------------ */

void EditorController::viewportDidRequestDisplayOptions(View *anchor) {
    showDisplayOptions(anchor);
}

// The display options of the viewport: the VIEWPORT section of the editor scope in a popover next
// to the gear (the same section the Editor Settings document shows).
void EditorController::showDisplayOptions(View *anchor) {
    Context *context = m_root ? m_root->getContext() : nullptr;
    if (!context || !anchor) { return; }
    auto sections = std::make_shared<SettingsSectionsView>(Panda::SettingsScope::Editor);
    sections->setTitleFilter([](const std::string &title) { return title == "VIEWPORT"; });
    sections->setStore(&settings(Panda::SettingsScope::Editor));
    sections->setSizesToContent(true);
    auto card = std::make_shared<Panel>();
    applyElevation(*card, Elevation::Popover);
    // As tall as the section and no taller than the window the popup covers: only what does not
    // fit scrolls. A row added to the section needs no new size here.
    card->layout().setWidth(Length::points(300.f));
    card->layout().setMaxHeight(Length::percent(100.f));
    card->layout().setFlexDirection(FlexDirection::Column);
    card->layout().setPadding(4.f);
    card->addSubview(sections);
    // Under the button, or above it when the window has no room below (Popup).
    context->addOverlay(std::make_shared<Popup>(card, anchor->getWorldFrame()));
}

Panda::TypeContext &EditorController::settings(Panda::SettingsScope scope) {
    return m_app.settingsStore(scope);
}

void EditorController::commitSettings(Panda::SettingsScope scope, Foundation::Shared<Panda::WorldCommand> command) {
    if (!command) { return; }
    switch (scope) {
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
        case Panda::SettingsScope::Project: showSettings(SettingsPage::Project, section); return;
        case Panda::SettingsScope::Editor: showSettings(SettingsPage::Editor, section); return;
    }
}

void EditorController::showMain(const std::string &component) {
    const Panda::ComponentType *type = Panda::GameContext::getExtensions().components.find(component);
    if (type == nullptr || m_panelsWorld == nullptr) { return; }
    const Panda::Entity main = m_panelsWorld->findMain(*type);
    if (!main.isValid()) {
        const ComponentInspector *inspector = editorExtensions().componentInspectors.find(component);
        LOG_WARN_EDITOR(
            "The world has no %s: Add Component \xE2\x86\x92 %s on an entity of it.",
            inspector != nullptr ? inspector->displayName.c_str() : component.c_str(),
            inspector != nullptr ? inspector->displayName.c_str() : component.c_str()
        );
        return;
    }
    if (m_worldTabs) { m_worldTabs->showActive(); }
    setInspectorVisible(true);
    selectEntities({main.getId()});
}

void EditorController::toolbarDidExport() {
    showExportWindow();
}

// Undo/redo go by the stack of the ACTIVE DOCUMENT (the document model): a document tab —
// its stack (a world tab's — the tree, the Scene and the inspector in it — is the world's), the
// viewport session — the document of the brush, otherwise the editing world. Called both by the
// toolbar and by the menu — the same semantics regardless of the UI.
Document *EditorController::activeDocument() {
    // The focus moves with every click (Context) — "where the user is" is reliable. The selected
    // document tab of the dock with the focus — its document; a side dock without one (the Content
    // Browser) — the world.
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
    navigateSelection(-1);
}

void EditorController::toolbarDidNavigateForward() {
    navigateSelection(+1);
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
// Content Browser, the inspector, a settings page) or nowhere, the same keys mean something else
// or nothing: Backspace there must not delete what is selected in the world.
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
    action("file.import", "File", "Import Files", [this] {
        if (m_assetActions) { m_assetActions->contentDidRequestImportDialog(m_contentView->currentDirectory()); }
    });
    // Find is of the panel the keyboard is in: the Content Browser takes it while it has the focus
    // (registered first, on the same key), the Hierarchy otherwise.
    command("contentBrowser.search", "Editor", "Find in Content Browser", [this] {
        if (!m_contentView->hasKeyboard()) { return false; }
        m_contentView->focusSearch();
        return true;
    });
    // The keys of the entities mean the files while the keyboard is in the Content Browser: the
    // same chords, registered first.
    command("contentBrowser.delete", "Assets", "Delete", [this] {
        return m_contentView->hasKeyboard() && m_contentView->requestDeleteSelection();
    });
    command("contentBrowser.rename", "Assets", "Rename", [this] {
        return m_contentView->hasKeyboard() && m_contentView->requestRenameSelection();
    });
    command("contentBrowser.duplicate", "Assets", "Duplicate", [this] {
        return m_contentView->hasKeyboard() && m_contentView->requestDuplicateSelection();
    });
    // The rows of its list, larger and smaller: tall rows show the pictures of the assets.
    command("contentBrowser.zoomIn", "Assets", "Larger Rows of the List", [this] {
        return m_contentView->hasKeyboard() && m_contentView->zoomList(+1);
    });
    command("contentBrowser.zoomOut", "Assets", "Smaller Rows of the List", [this] {
        return m_contentView->hasKeyboard() && m_contentView->zoomList(-1);
    });
    command("hierarchy.search", "Editor", "Find in Hierarchy", [this] {
        if (!showPanel("hierarchy")) { return false; }
        m_hierarchyView->focusSearch();
        return true;
    });
    command("tab.close", "Window", "Close Tab", [this] { return closeFocusedTab(); });
    command("dock.toggleFullscreen", "Window", "Toggle Fullscreen of the Panel",
            [this] { return maximizeFocusedDock(); });
    // The parts of the window, shown and hidden: the files, the tree and the inspector of the world
    // tab, the output under the center.
    action("window.toggleFiles", "Window", "Toggle Files", [this] { m_dockManager->toggle(Dock::Left); m_dockManager->refreshStrip(); });
    action("window.toggleHierarchy", "Window", "Toggle Hierarchy", [this] { setTreeVisible(!m_worldView->isTreeVisible()); });
    action("window.toggleInspector", "Window", "Toggle Inspector", [this] { setInspectorVisible(!m_worldView->isInspectorVisible()); });
    action("window.toggleBottomPanel", "Window", "Toggle Bottom Panel", [this] {
        m_dockManager->toggle(Dock::Bottom);
        m_dockManager->refreshStrip();
    });
    // The middle of the world tab: the switch of the toolbar, by name.
    action("window.world", "Window", "World", [this] { showWorldView(); });
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
        {"hierarchy", "Hierarchy"}, {"inspector", "Inspector"}, {"contentBrowser", "Content Browser"},
        {"output", "Output"},       {"cmake", "CMake"},         {"statistics", "Statistics"},
        {"profiler", "Profiler"},
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
// name. An entity becomes the selection, an asset opens in its document and is shown where it lives.
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
        actions->push_back([this, id] { openAsset(id); });
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

void EditorController::tabsAssetTabOpened(Panda::AssetId id) {
    if (auto *handler = Panda::AssetHandlerEditor::current()) { handler->setAssetOpenedInTab(id, true); }
}

void EditorController::tabsAssetTabClosed(Panda::AssetId id) {
    if (auto *handler = Panda::AssetHandlerEditor::current()) { handler->setAssetOpenedInTab(id, false); }
    // A viewport session without the tab of its document is an invisible LMB capture: the tool lets go.
    if (m_viewportEditingDocument == id) { viewportDidRequestStopEditing(); }
}

bool EditorController::tabsSaveAsset(Panda::AssetId id) {
    return m_app.documentActions().saveAsset(id);
}

void EditorController::tabsDocumentClosed(Document *document) {
    if (m_lastUndoDocument == document) { m_lastUndoDocument = nullptr; }
    m_worldTabs->documentClosed(document);
}

} // namespace Editor

