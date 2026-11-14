#include "UI/EditorController.hpp"

#include <Panda/Serialization/WorldMapper.hpp>
#include <Rain/Coders/JsonDecoder.hpp>
#include <Rain/Coders/JsonEncoder.hpp>
#include <sstream>
#include <Foundation/GraphicsAPI.hpp>

#include "Preview/ThumbnailsManager.hpp"
#include "UI/Documents/CoreAssetEditors.hpp"
#include "UI/Documents/PluginsDocument.hpp"
#include "UI/Documents/SettingsDocuments.hpp"
#include "UI/Documents/UiGalleryDocument.hpp"
#include "UI/EditorDialogs.hpp"
#include "UI/Settings/CoreSettingsSections.hpp"
#include <PandaEditorKit/Widgets/Icons.hpp>
#include "Project/Build/ProjectBuilder.hpp"
#include "Project/Loader/AssetHandlerEditor.hpp"
#include "Project/System/SystemTools.hpp"

#include <PandaEditorKit/ComponentCommands.hpp>
#include "PandaUI/Elements/Popup.hpp"
#include "PandaUI/Elements/TabView.hpp"
#include "PandaUI/Runtime/Context.hpp"

#include <Panda/Assets/Base/AssetImporterBase.hpp>
#include <Panda/Assets/MaterialAsset.hpp>
#include <Panda/Assets/StaticResources.hpp>
#include <Panda/Assets/PostprocessingAsset.hpp>
#include <Panda/GameLogic/Components/BaseComponents.hpp>
#include <Panda/GameLogic/Components/TransformComponent.hpp>
#include <Panda/GameLogic/Entity.hpp>
#include <Panda/GameLogic/GameContext.hpp>
#include <Panda/GameLogic/SelectionContext.hpp>
#include <Panda/GameLogic/World.hpp>
#include <Panda/ScriptEngine/ExternalScript.hpp>
#include <Panda/ScriptEngine/ScriptEngine.hpp>
#include <Panda/WorldCommands/Impl/AddRemoveComponentCommand.hpp>
#include <Panda/WorldCommands/Impl/AddRemoveEntitiesCommand.hpp>
#include <Panda/WorldCommands/Impl/EditAssetCommands.hpp>
#include <Panda/WorldCommands/Impl/EditScriptFieldCommand.hpp>
#include <Panda/WorldCommands/Impl/SetMeshSlotMaterialCommand.hpp>
#include <Panda/WorldCommands/Impl/EntityTransformCommand.hpp>
#include <Panda/WorldCommands/Impl/HierarchyCommand.hpp>
#include <Panda/WorldCommands/CommandHistory.hpp>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace Editor {

using namespace PandaUI;

namespace {

    // The selection with its subtrees (the Static flag is inherited by children, as in Unity).
    std::unordered_set<Panda::Entity> collectSubtree(Panda::World &world, const std::vector<Panda::Entity> &roots) {
        std::unordered_set<Panda::Entity> result;
        std::vector<Panda::Entity> stack(roots.begin(), roots.end());
        while (!stack.empty()) {
            Panda::Entity entity = stack.back();
            stack.pop_back();
            if (!entity.isValid() || !result.insert(entity).second) { continue; }
            for (Panda::UUID childId : entity.getChildEntities()) {
                Panda::Entity child = world.getById(childId);
                if (child.isValid()) { stack.push_back(child); }
            }
        }
        return result;
    }

    // Default value of a script field by type (for the fields of a new ExternalScript).
    Panda::ScriptFieldValue defaultScriptValue(Panda::ScriptFieldType type) {
        switch (type) {
            case Panda::ScriptFieldType::INTEGER: return int32_t(0);
            case Panda::ScriptFieldType::FLOAT: return 0.f;
            default: return Panda::UUID(0); // ENTITY/TEXTURE/MATERIAL/WORLD/UNKNOWN — a reference by UUID
        }
    }

    Panda::AssetHandlerEditor *assetHandler() {
        return static_cast<Panda::AssetHandlerEditor *>(Panda::GameContext::getAssetHandler());
    }

    // The coordinator of the process: the document types of the registry are created through it
    // (the registry holds plain callables, the coordinator is created once).
    EditorController *s_controller = nullptr;

    constexpr const char *PROJECT_SETTINGS_DOCUMENT = "projectSettings";
    constexpr const char *EDITOR_SETTINGS_DOCUMENT = "editorSettings";
    constexpr const char *PLUGINS_DOCUMENT = "plugins";
    constexpr const char *UI_GALLERY_DOCUMENT = "uiGallery";
    constexpr const char *GAME_DOCUMENT = "game";
    constexpr const char *GAME_TAB_ID = "doc:game"; // EditorTabs names a document tab doc:<type>
    // The tab of an open world: world:<id of the open world>. The ids live for one run, so a saved
    // layout only remembers that a world was in front of the center (WORLD_TAB_MARKER; "viewport"
    // is how the layouts saved before the world tabs name it).
    constexpr const char *WORLD_TAB_PREFIX = "world:";
    constexpr const char *WORLD_TAB_MARKER = "world";
    constexpr const char *LEGACY_SCENE_TAB_ID = "viewport";

    std::string worldTabId(uint32_t worldId) {
        return WORLD_TAB_PREFIX + std::to_string(worldId);
    }
    bool isWorldTab(const std::string &tabId) {
        return tabId.rfind(WORLD_TAB_PREFIX, 0) == 0;
    }
    uint32_t worldIdOfTab(const std::string &tabId) {
        return static_cast<uint32_t>(std::strtoul(tabId.c_str() + std::strlen(WORLD_TAB_PREFIX), nullptr, 10));
    }

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

// A world document: the tab of one open world. Dirty is the world's change flag, Save writes the
// world file, the undo stack is the world's. The tab content is a host panel: the one viewport of
// the editor lives in the host of the active world (EditorController::syncWorldTabs), the other
// hosts are empty.
class WorldDocument final : public Document {
public:
    WorldDocument(EditorController *controller, uint32_t worldId)
        : m_controller(controller)
        , m_worldId(worldId)
        , m_host(std::make_shared<Panel>()) {
        m_host->setBackgroundColor(ThemeColor::windowBackground);
        m_host->layout().setFlexDirection(FlexDirection::Column);
        m_host->layout().setFlexGrow(1.f);
    }

    uint32_t worldId() const {
        return m_worldId;
    }
    const std::shared_ptr<Panel> &host() const {
        return m_host;
    }

    // The name of the file the tab edits, as every document tab shows it.
    std::string title() override {
        const OpenWorld *world = m_controller->openWorld(m_worldId);
        return world ? world->name : "World";
    }
    std::string icon() override {
        return Ic::PanelViewport;
    }
    std::shared_ptr<PandaUI::View> view() override {
        return m_host;
    }
    bool isDirty() override {
        const OpenWorld *world = m_controller->openWorld(m_worldId);
        return world && world->dirty;
    }
    bool save() override {
        return m_controller->m_bridge && m_controller->m_bridge->saveWorld(m_worldId);
    }
    std::filesystem::path filePath() override {
        const OpenWorld *world = m_controller->openWorld(m_worldId);
        return world ? world->path : std::filesystem::path();
    }
    Panda::CommandHistory *undoStack() override {
        // The active world is what the panels show: in Play that is the playing copy, and its
        // edits go away with it.
        if (isActive()) {
            return m_controller->m_panelsWorld ? &m_controller->m_panelsWorld->getCommandHistory() : nullptr;
        }
        const OpenWorld *world = m_controller->openWorld(m_worldId);
        return world && world->world ? &world->world->getCommandHistory() : nullptr;
    }
    void refresh() override {
        if (!isActive()) { return; } // the panels show the active world
        if (m_controller->m_panelsWorld) { m_controller->m_panelsWorld->setChanged(); }
        m_controller->refreshFromWorld();
    }

private:
    bool isActive() const {
        return m_controller->m_bridge && m_controller->m_bridge->activeWorldId() == m_worldId;
    }

    EditorController *m_controller;
    uint32_t m_worldId;
    std::shared_ptr<Panel> m_host;
};

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

EditorController::EditorController()
    : m_hostBinding(this)
    , m_hierarchyView(std::make_shared<HierarchyPanelView>(this))
    , m_contentView(std::make_shared<ContentBrowserView>(this))
    , m_toolbarView(std::make_shared<ToolbarPanelView>(this))
    , m_outputView(std::make_shared<OutputPanelView>())
    , m_cmakeView(std::make_shared<CMakePanelView>(this))
    , m_statisticsView(std::make_shared<StatisticsPanelView>())
    , m_profilerView(std::make_shared<ProfilerPanelView>()) {
    s_controller = this;
    // The core goes through the same registries as the modules (context/plugins.md, decision 2):
    // the settings sections, the asset editors and the document types — once per process, before
    // the editor parts of the modules add theirs.
    static bool s_coreRegistered = false;
    if (!s_coreRegistered) {
        s_coreRegistered = true;
        registerCoreSettingsSections(editorExtensions());
        registerCoreAssetKinds(editorExtensions());
        registerDocumentTypes();
    }
    buildLayout();
    registerShortcuts();
    pushSelectionToPanels(); // initial state: empty → status "-"
    refreshStatistics();
}


EditorController::~EditorController() {
    if (s_controller == this) { s_controller = nullptr; }
} // m_hostBinding unbinds the host last

void EditorController::registerDocumentTypes() {
    DocumentType project;
    project.id = PROJECT_SETTINGS_DOCUMENT;
    project.title = "Project Settings";
    project.icon = Ic::Configure;
    project.create = [] {
        ProjectSettingsDocument::Host host;
        host.store = []() -> Panda::TypeContext & { return s_controller->settings(Panda::SettingsScope::Project); };
        host.isDirty = [] { return s_controller->m_bridge && s_controller->m_bridge->isProjectSettingsDirty(); };
        host.markDirty = [] {
            if (s_controller->m_bridge) { s_controller->m_bridge->projectSettingsEdited(); }
        };
        host.save = [] { return s_controller->m_bridge ? s_controller->m_bridge->saveProjectSettings() : false; };
        host.filePath = [] {
            return s_controller->m_bridge ? s_controller->m_bridge->settingsFilePath(Panda::SettingsScope::Project)
                                          : std::filesystem::path();
        };
        host.revert = [] {
            if (s_controller->m_bridge) { s_controller->m_bridge->revertProjectSettings(); }
        };
        return std::unique_ptr<Document>(std::make_unique<ProjectSettingsDocument>(std::move(host)));
    };
    editorExtensions().documentTypes.add(std::move(project));

    DocumentType editor;
    editor.id = EDITOR_SETTINGS_DOCUMENT;
    editor.title = "Editor Settings";
    editor.icon = Ic::Configure;
    editor.create = [] {
        return std::unique_ptr<Document>(
            std::make_unique<EditorSettingsDocument>(s_controller->settings(Panda::SettingsScope::Editor), [] {
                return s_controller->m_bridge ? s_controller->m_bridge->settingsFilePath(Panda::SettingsScope::Editor)
                                              : std::filesystem::path();
            })
        );
    };
    editorExtensions().documentTypes.add(std::move(editor));

    // The Plugins tab: the store and the theme live in the application layer, behind the bridge.
    DocumentType plugins;
    plugins.id = PLUGINS_DOCUMENT;
    plugins.title = "Plugins";
    plugins.icon = Ic::Plugins;
    plugins.create = [] {
        PluginsHost host;
        if (EngineBridge *bridge = s_controller->m_bridge) {
            host.store = &bridge->pluginStore();
            host.currentTheme = [bridge] { return bridge->currentThemeId(); };
            host.selectTheme = [bridge](const std::string &id) { bridge->selectTheme(id); };
            host.installDevPlugin = [bridge] { bridge->installDevPlugin(); };
        }
        return std::unique_ptr<Document>(std::make_unique<PluginsDocument>(std::move(host)));
    };
    editorExtensions().documentTypes.add(std::move(plugins));

    // The UI Gallery tab (PandaUI/examples/gallery): the textures and the display face come from the
    // UI host of the application layer, behind the bridge.
    DocumentType gallery;
    gallery.id = UI_GALLERY_DOCUMENT;
    gallery.title = "UI Gallery";
    gallery.create = [] {
        PandaUI::Gallery::Host host;
        if (EngineBridge *bridge = s_controller->m_bridge) { host = bridge->galleryHost(); }
        return std::unique_ptr<Document>(std::make_unique<UiGalleryDocument>(host));
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

ViewportSurfaces EditorController::viewportSurfaces() const {
    ViewportSurfaces surfaces;
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
}

void EditorController::showSceneView() {
    if (m_dockManager && m_bridge) { m_dockManager->showPanel(worldTabId(m_bridge->activeWorldId())); }
}

/* ------------------------------ World tabs ------------------------------ */

const OpenWorld *EditorController::openWorld(uint32_t worldId) const {
    for (const OpenWorld &world : m_openWorlds) {
        if (world.id == worldId) { return &world; }
    }
    return nullptr;
}

WorldDocument *EditorController::worldDocument(uint32_t worldId) {
    // Only this coordinator opens tabs with the world prefix, and only with world documents.
    Document *document = m_editorTabs ? m_editorTabs->documentForTab(worldTabId(worldId)) : nullptr;
    return static_cast<WorldDocument *>(document);
}

Document *EditorController::activeWorldDocument() {
    return m_bridge ? worldDocument(m_bridge->activeWorldId()) : nullptr;
}

void EditorController::syncWorldTabs() {
    if (!m_bridge || !m_editorTabs || !m_dockManager || m_inWorldSync) { return; }
    m_inWorldSync = true;
    // The worlds whose tabs the user closed leave the list of the project (their documents have
    // asked about the edits). The tab the user landed on says which world is shown next.
    if (!m_closedWorldTabs.empty()) {
        const std::vector<uint32_t> closed = std::move(m_closedWorldTabs);
        m_closedWorldTabs.clear();
        for (uint32_t worldId : closed) {
            if (worldId == m_bridge->activeWorldId() && m_bridge->canActivateWorld()) {
                const std::string selected = m_dockManager->selectedPanelId(Dock::Center);
                if (isWorldTab(selected)) { m_bridge->activateWorld(worldIdOfTab(selected)); }
            }
            m_bridge->closeWorld(worldId);
        }
    }
    m_openWorlds = m_bridge->openWorlds();
    // The tabs follow the list: the tab of a world that is gone closes, a new world gets a tab.
    m_closingWorldTab = true;
    for (size_t i = 0; i < m_worldTabs.size();) {
        if (openWorld(m_worldTabs[i]) != nullptr) {
            ++i;
            continue;
        }
        m_editorTabs->closeTab(worldTabId(m_worldTabs[i]));
        m_worldTabs.erase(m_worldTabs.begin() + static_cast<std::ptrdiff_t>(i));
    }
    m_closingWorldTab = false;
    bool opened = false;
    for (const OpenWorld &world : m_openWorlds) {
        const std::string tabId = worldTabId(world.id);
        if (std::find(m_worldTabs.begin(), m_worldTabs.end(), world.id) == m_worldTabs.end()) {
            m_worldTabs.push_back(world.id);
            m_editorTabs->openDocument(tabId, std::make_unique<WorldDocument>(this, world.id), Dock::Center);
            opened = true;
        }
        m_dockManager->setPanelTitle(tabId, world.name); // a no-op while the name stands (Save As changes it)
    }
    // The active world and the selected tab follow each other.
    const uint32_t activeId = m_bridge->activeWorldId();
    const std::string activeTab = worldTabId(activeId);
    if (activeId != m_shownWorldId || opened) {
        // The active world changed outside the tabs (a world was opened, created or closed): its
        // tab comes forward. A tab that has just been added selected itself — the active one stays.
        m_shownWorldId = activeId;
        m_dockManager->activateTransientPanel(activeTab);
    } else {
        const std::string selected = m_dockManager->selectedPanelId(Dock::Center);
        if (isWorldTab(selected) && selected != activeTab) {
            if (m_bridge->canActivateWorld()) {
                m_bridge->activateWorld(worldIdOfTab(selected));
                m_shownWorldId = m_bridge->activeWorldId();
            } else {
                // The game plays a copy of the active world: the tabs do not switch until it stops.
                m_dockManager->activateTransientPanel(activeTab);
                m_outputView->appendLog(OutputPanelView::LogLevel::Warning, "Stop the game to switch to another world");
            }
        }
    }
    // The one viewport of the editor lives in the tab of the active world.
    WorldDocument *active = worldDocument(m_bridge->activeWorldId());
    if (active != nullptr && m_viewportView && m_viewportView->getSuperview() != active->host().get()) {
        Context *context = m_root ? m_root->getContext() : nullptr;
        View *focused = context ? context->getFocusedView() : nullptr; // a re-parent drops the focus
        active->host()->addSubview(m_viewportView);
        if (context && focused) { context->setFocusedView(focused); }
    }
    m_inWorldSync = false;
}

void EditorController::focusGameView() {
    if (!m_gameView || !m_root) { return; }
    if (Context *context = m_root->getContext()) { context->setFocusedView(m_gameView.get()); }
}

Panda::DockLayoutState EditorController::captureDockLayout() const {
    Panda::DockLayoutState state = m_dockManager->captureLayout();
    // Which world is in front is the project session's to say: the layout only remembers that
    // one was.
    if (isWorldTab(state.selectedCenter)) { state.selectedCenter = WORLD_TAB_MARKER; }
    state.worldSettingsVisible = m_viewportView && m_viewportView->isSidebarVisible();
    return state;
}

void EditorController::applyDockLayout(const Panda::DockLayoutState &state) {
    syncWorldTabs(); // the world tabs first: the pinned tabs of the center follow them
    m_dockManager->applyLayout(state);
    // Pinned editor tabs: restored after the layout (the world is already set —
    // EditorLayer calls setWorld before applyDockLayout; dead targets are skipped).
    if (m_editorTabs) { m_editorTabs->restorePinned(state.pinnedTabs); }
    if (state.isValid()) {
        // Every restored tab selected itself as it opened: the active tabs of the session go back on
        // top, and the keyboard returns to the dock it was in (the active document of undo).
        m_dockManager->applySelection(state);
        if (m_bridge && (state.selectedCenter == WORLD_TAB_MARKER || state.selectedCenter == LEGACY_SCENE_TAB_ID)) {
            m_dockManager->activateTransientPanel(worldTabId(m_bridge->activeWorldId()));
        }
        if (state.focusedDock >= 0 && state.focusedDock <= 3) {
            m_dockManager->focusDock(static_cast<Dock>(state.focusedDock));
        }
    }
    setWorldSettingsVisible(state.worldSettingsVisible);
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
    refreshStatistics();
    pushSelectionToPanels();
}

void EditorController::syncWorldStructure() {
    if (!m_panelsWorld) { return; }
    const uint64_t version = m_panelsWorld->getStructureVersion();
    if (version == m_lastStructureVersion) { return; }
    m_lastStructureVersion = version;
    // The selection is the editor's reaction to the world structure: deleted entities (destroy, clear,
    // scripts in Play) leave the selection here, not inside World.
    for (Panda::UUID id : Panda::SelectionContext::getSelectedEntities()) {
        if (!m_panelsWorld->isValid(id)) { Panda::SelectionContext::removeSelectedEntity(id); }
    }
    m_hierarchyView->reload();
    refreshStatistics();
}

void EditorController::refreshFromWorld() {
    if (m_panelsWorld) { m_lastStructureVersion = m_panelsWorld->getStructureVersion(); }
    m_hierarchyView->reload();
    refreshStatistics();
    pushSelectionToPanels();
    if (m_worldSettingsView) { m_worldSettingsView->rebuild(); } // a loaded world, undo/redo: the values changed
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
    refreshSettingsDocuments();
    if (m_statisticsView) { m_statisticsView->rebuildExtensionSections(); }
}

void EditorController::restartEditor() {
    if (m_bridge) { m_bridge->restartEditor(); }
}

void EditorController::saveActiveDocument() {
    // Cmd+S: the document of the tab with the focus (an entity tab and a side dock mean the world).
    if (Document *document = activeDocument()) { document->save(); }
}

void EditorController::addFileItems(Menu &menu, const std::filesystem::path &file) {
    if (file.empty() || !m_bridge) { return; }
    // The path as the project sees it: relative to the project folder when the file is inside.
    std::string shown = file.generic_string();
    if (Panda::AssetHandlerEditor *handler = assetHandler()) {
        const std::filesystem::path project = handler->getProjectPath();
        std::error_code errorCode;
        const std::filesystem::path relative = std::filesystem::relative(file, project, errorCode);
        if (!project.empty() && !errorCode && !relative.empty() && *relative.begin() != "..") {
            shown = relative.generic_string();
        }
    }
    menu.addItem("Copy Path: " + shown, [file] { setClipboardText(file.string()); });
    menu.addItem("Open in External Editor", [this, file] {
        if (m_bridge) { m_bridge->openInExternalEditor(file); }
    });
    menu.addItem("Reveal in File Manager", [this, file] {
        if (m_bridge) { m_bridge->revealInFileManager(file); }
    });
}

void EditorController::settingsFileReloaded(Panda::SettingsScope scope) {
    if (!m_editorTabs) { return; }
    if (scope == Panda::SettingsScope::Project) {
        auto *project = dynamic_cast<ProjectSettingsDocument *>(m_editorTabs->documentOfType(PROJECT_SETTINGS_DOCUMENT));
        if (project) { project->fileReloaded(); }
    } else if (Document *editor = m_editorTabs->documentOfType(EDITOR_SETTINGS_DOCUMENT)) {
        editor->refresh();
    }
}

void EditorController::refreshSettingsDocuments() {
    if (!m_editorTabs) { return; }
    for (const char *typeId : {PROJECT_SETTINGS_DOCUMENT, EDITOR_SETTINGS_DOCUMENT}) {
        if (Document *document = m_editorTabs->documentOfType(typeId)) { document->refresh(); }
    }
}

void EditorController::worldContextChanged() {
    m_lastUndoDocument = nullptr; // the undo target pointed into the previous context
    viewportDidRequestStopEditing(); // a viewport session edits what the previous world showed: the tool lets go
    if (!m_editorTabs) { return; }
    m_editorTabs->closeAllEntityTabs();
    m_editorTabs->clearHistory();
}

void EditorController::setWorldDisplayName(const std::string &name) {
    if (m_hierarchyView) { m_hierarchyView->setRootTitle(name); }
}

void EditorController::thumbnailDidBecomeReady(Panda::AssetId assetId) {
    m_contentView->thumbnailDidBecomeReady(assetId);
}

void EditorController::refreshAssetPanels() {
    m_contentView->refresh();
}

/* ----------------------------- Selection flow ---------------------------- */

std::vector<Panda::Entity> EditorController::selectedEntities() {
    std::vector<Panda::Entity> result;
    if (!m_panelsWorld) { return result; }
    if (m_entityTargetActive) { // an edit from a tab: the target is its objects, not the global selection
        for (Panda::UUID id : m_entityTargetOverride) {
            Panda::Entity e = m_panelsWorld->getById(id);
            if (e.isValid()) { result.push_back(e); }
        }
        return result;
    }
    for (Panda::UUID id : Panda::SelectionContext::getSelectedEntities()) {
        Panda::Entity e = m_panelsWorld->getById(id);
        if (e.isValid()) { result.push_back(e); }
    }
    return result;
}

Panda::Entity EditorController::primaryEntity() {
    for (Panda::Entity e : selectedEntities()) { return e; }
    return Panda::Entity();
}

void EditorController::pushSelectionToPanels() {
    // Remember the selection and transform versions → the per-frame sync does not re-trigger the panels
    // on own changes.
    m_lastSelectionVersion = Panda::SelectionContext::getEntitySelectionVersion();
    m_lastTransformVersion = Panda::SelectionContext::getTransformVersion();

    std::vector<Panda::Entity> entities = selectedEntities();
    if (!entities.empty()) {
        std::vector<Panda::UUID> ids;
        for (Panda::Entity &e : entities) { ids.push_back(e.getId()); }
        m_hierarchyView->setSelectedEntities(ids);
        // A single selection is found in the hierarchy: the ancestors open, the row scrolls into
        // view (a click on a visible row changes nothing). A multi-selection has no primary
        // entity to show — the set keeps no order — so it moves nothing.
        if (ids.size() == 1) { m_hierarchyView->revealEntity(ids.front()); }
        m_contentView->clearAssetSelection();
        m_hierarchyView->setStatusText(entities.size() == 1
                                           ? "Selected: " + entities.front().getName()
                                           : "Selected: " + std::to_string(entities.size()) + " items");
    } else {
        m_hierarchyView->setSelectedEntities({});
        m_hierarchyView->setStatusText("Selected: -");
    }
    // Editor tabs: the selection opens/replaces the preview tab (the material preview is there too).
    if (m_editorTabs) { m_editorTabs->showSelection(entities, entities.empty() ? m_selectedAssets : std::vector<Panda::AssetId>{}); }
}

void EditorController::selectEntitiesByIds(const std::vector<Panda::UUID> &ids) {
    Panda::SelectionContext::unselectAll();
    for (Panda::UUID id : ids) { Panda::SelectionContext::addSelectedEntity(id); }
    m_selectedAssets.clear();
    pushSelectionToPanels();
}

void EditorController::deselectAll() {
    Panda::SelectionContext::unselectAll();
    m_selectedAssets.clear();
    m_contentView->clearAssetSelection();
    pushSelectionToPanels();
}

void EditorController::syncSelectionFromEngine() {
    if (!m_panelsWorld) { return; }
    if (Panda::SelectionContext::getEntitySelectionVersion() != m_lastSelectionVersion) {
        m_selectedAssets.clear();   // a viewport selection means entities; assets are deselected
        pushSelectionToPanels();    // re-captures m_lastSelectionVersion and m_lastTransformVersion
        return;
    }
    // Same selection, but the transform may have changed externally (gizmo drag) — update only the
    // Transform fields.
    const std::uint64_t transformVersion = Panda::SelectionContext::getTransformVersion();
    if (transformVersion != m_lastTransformVersion) {
        m_lastTransformVersion = transformVersion;
        if (m_editorTabs) { m_editorTabs->refreshTransforms(); }
    }
    if (m_editorTabs) { m_editorTabs->syncLifetimes(); } // tab targets died → the tabs are closed
}

void EditorController::updateChromeFromState(bool isEditMode) {
    if (!m_toolbarView) { return; }
    syncWorldTabs();
    Panda::ScriptEngine *engine = Panda::GameContext::getScriptEngine();
    const bool scriptingReady = engine && engine->isLoaded();
    // The undo/redo buttons show the stack state of the ACTIVE document (just like Cmd+Z itself).
    Document *document = activeDocument();
    Panda::CommandHistory *stack = document ? document->undoStack() : nullptr;
    const bool canUndo = stack && stack->CAN_UNDO();
    const bool canRedo = stack && stack->CAN_REDO();
    const bool canPlay = isEditMode && scriptingReady; // Play: only from Edit and with scripting loaded
    const bool canStop = !isEditMode;                  // Stop: only while the world is running

    if (canUndo != m_tbCanUndo) { m_tbCanUndo = canUndo; m_toolbarView->setCanUndo(canUndo); }
    if (canRedo != m_tbCanRedo) { m_tbCanRedo = canRedo; m_toolbarView->setCanRedo(canRedo); }
    if (canPlay != m_tbCanPlay) { m_tbCanPlay = canPlay; m_toolbarView->setCanPlay(canPlay); }
    if (canStop != m_tbCanStop) { m_tbCanStop = canStop; m_toolbarView->setCanStop(canStop); }
    if (isEditMode != m_tbCanReload) {
        m_tbCanReload = isEditMode;
        m_toolbarView->setCanReloadScripts(isEditMode);
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
    // (syncWorldTabs), and the viewport lives in the tab of the active one.
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

    m_dockManager = std::make_unique<DockManager>(m_toolbarView, centerTabs, std::move(panels));
    m_editorTabs = std::make_unique<EditorTabs>(m_dockManager.get(), this);
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
        for (const DocumentType &type : editorExtensions().documentTypes.types()) {
            const std::string id = type.id;
            items.push_back({type.title, [this, id](Dock dock) { m_editorTabs->openDocumentType(id, dock); }});
        }
        return items;
    });
    m_root = m_dockManager->rootView();
}

/* --------------------------- Hierarchy outputs --------------------------- */

void EditorController::hierarchyDidSelectEntities(const std::vector<Panda::UUID> &ids) {
    selectEntitiesByIds(ids);
}

CreateActions EditorController::hierarchyCreateActions(Panda::UUID parentId) {
    CreateActions actions;
    actions.push_back({"Empty Entity", [this, parentId] { createEntity(parentId); }});
    const std::pair<const char *, PrimitiveKind> primitives[] = {
        {"Cube", PrimitiveKind::Cube},       {"Sphere", PrimitiveKind::Sphere},     {"Capsule", PrimitiveKind::Capsule},
        {"Plane", PrimitiveKind::Plane},     {"Cylinder", PrimitiveKind::Cylinder},
    };
    for (const auto &[title, kind] : primitives) {
        actions.push_back({title, [this, parentId, kind = kind] { createPrimitive(parentId, kind); }});
    }
    return actions;
}

CreateActions EditorController::newEntityActions() {
    return m_panelsWorld ? hierarchyCreateActions(Panda::UUID(0)) : CreateActions();
}

CreateActions EditorController::newAssetActions() {
    // The titles do not depend on the folder: the menu is built before the browser has one.
    if (!assetHandler()) { return {}; }
    CreateActions actions = contentCreateActions(m_contentView ? m_contentView->currentDirectory() : Panda::path_t());
    for (CreateAction &action : actions) {
        // The new asset appears in the folder the browser shows: the browser comes forward.
        action.run = [this, run = std::move(action.run)] {
            if (m_dockManager) { m_dockManager->showPanel("contentBrowser"); }
            run();
        };
    }
    return actions;
}

void EditorController::createEntity(Panda::UUID parentId) {
    if (!m_panelsWorld) { return; }
    Panda::Entity created = m_panelsWorld->instantiateEntity();
    created.setName("GameObject " + std::to_string(m_nextEntity++));
    m_panelsWorld->setParent(created, m_panelsWorld->getById(parentId)); // no such parent — a root
    Panda::AddRemoveEntitiesCommand command({created});
    m_panelsWorld->getCommandHistory().SAVE(command, false); // entity already created — the command is only for undo
    m_panelsWorld->setChanged();
    const Panda::UUID newId = created.getId();
    refreshFromWorld();
    selectEntitiesByIds({newId});
}

void EditorController::createPrimitive(Panda::UUID parentId, PrimitiveKind kind) {
    if (!m_panelsWorld) { return; }
    struct PrimitiveSpec {
        const char *name;
        Panda::PrimitiveMeshKind meshKind;
    };
    const PrimitiveSpec spec = [kind]() -> PrimitiveSpec {
        switch (kind) {
            case PrimitiveKind::Sphere: return {"Sphere", Panda::PrimitiveMeshKind::Sphere};
            case PrimitiveKind::Capsule: return {"Capsule", Panda::PrimitiveMeshKind::Capsule};
            case PrimitiveKind::Plane: return {"Plane", Panda::PrimitiveMeshKind::Plane};
            case PrimitiveKind::Cylinder: return {"Cylinder", Panda::PrimitiveMeshKind::Cylinder};
            case PrimitiveKind::Cube:
            default: return {"Cube", Panda::PrimitiveMeshKind::Cube};
        }
    }();

    Panda::Entity created = m_panelsWorld->instantiateEntity();
    created.setName(std::string(spec.name) + " " + std::to_string(m_nextEntity++));
    m_panelsWorld->setParent(created, m_panelsWorld->getById(parentId)); // no such parent — a root
    Panda::PrimitiveMeshComponent &meshComponent = created.addComponent<Panda::PrimitiveMeshComponent>();
    meshComponent.kind = spec.meshKind; // empty material = the built-in checker
    // A collider is added by hand: the core knows no physics (the Physics module's components
    // come through the Add Component menu).

    Panda::AddRemoveEntitiesCommand command({created});
    m_panelsWorld->getCommandHistory().SAVE(command, false); // entity already created — the command is for undo
    m_panelsWorld->setChanged();
    const Panda::UUID newId = created.getId();
    refreshFromWorld();
    selectEntitiesByIds({newId});
}

void EditorController::hierarchyDidRequestDuplicate(const std::vector<Panda::UUID> &ids) {
    if (!m_panelsWorld) { return; }
    std::unordered_set<Panda::Entity> duplicates;
    std::vector<Panda::UUID> dupIds;
    for (Panda::UUID id : ids) {
        Panda::Entity src = m_panelsWorld->getById(id);
        if (!src.isValid()) { continue; }
        Panda::Entity dup = m_panelsWorld->duplicateEntity(src);
        duplicates.insert(dup);
        dupIds.push_back(dup.getId());
    }
    if (duplicates.empty()) { return; }
    Panda::AddRemoveEntitiesCommand command(duplicates);
    m_panelsWorld->getCommandHistory().SAVE(command, false);
    m_panelsWorld->setChanged();
    refreshFromWorld();
    selectEntitiesByIds(dupIds);
}

void EditorController::hierarchyDidRequestCopy(const std::vector<Panda::UUID> &ids) {
    if (!m_panelsWorld) { return; }
    std::vector<Panda::Entity> entities;
    for (Panda::UUID id : ids) {
        Panda::Entity entity = m_panelsWorld->getById(id);
        if (entity.isValid()) { entities.push_back(entity); }
    }
    if (entities.empty()) { return; }
    // The clipboard holds text: a fragment of a world file. It pastes into this world, into another
    // world or project (another editor window), and reads in any text editor.
    Rain::JsonEncoder encoder(true);
    std::ostringstream text;
    Panda::WorldMapper::saveEntities(*m_panelsWorld, entities, encoder, text);
    setClipboardText(text.str());
}

void EditorController::hierarchyDidRequestPaste() {
    if (!m_panelsWorld) { return; }
    const std::string text = getClipboardText();
    if (text.empty()) { return; }
    Rain::JsonDecoder decoder;
    std::istringstream stream(text);
    std::vector<Panda::Entity> added;
    if (!Panda::WorldMapper::loadEntities(*m_panelsWorld, decoder, stream, added) || added.empty()) {
        m_outputView->appendLog(
            OutputPanelView::LogLevel::Warning, "Paste — the clipboard holds no entities (a fragment of a world file)"
        );
        return;
    }
    std::unordered_set<Panda::Entity> roots(added.begin(), added.end());
    std::vector<Panda::UUID> ids;
    for (Panda::Entity entity : added) { ids.push_back(entity.getId()); }
    Panda::AddRemoveEntitiesCommand command(roots);
    m_panelsWorld->getCommandHistory().SAVE(command, false); // the entities exist — the command is for undo
    m_panelsWorld->setChanged();
    refreshFromWorld();
    selectEntitiesByIds(ids);
}

void EditorController::hierarchyDidRequestDelete(const std::vector<Panda::UUID> &ids) {
    if (!m_panelsWorld || ids.empty()) { return; }
    std::unordered_set<Panda::UUID> idSet(ids.begin(), ids.end());
    std::unordered_set<Panda::Entity> selected = m_panelsWorld->getById(idSet);
    Panda::AddRemoveEntitiesCommand command(selected);
    m_panelsWorld->getCommandHistory().SAVE(command, true); // execute = delete (with descendants — engine side)
    m_panelsWorld->setChanged();
    Panda::SelectionContext::unselectAll();
    refreshFromWorld();
}

void EditorController::hierarchyDidReparent(const std::vector<Panda::UUID> &items, Panda::UUID newParentId) {
    if (!m_panelsWorld) { return; }
    std::vector<Panda::Entity> entities;
    for (Panda::UUID id : items) {
        Panda::Entity ent = m_panelsWorld->getById(id);
        if (ent.isValid()) { entities.push_back(ent); }
    }
    if (entities.empty()) { return; }
    if (newParentId != Panda::UUID(0)) {
        Panda::Entity parent = m_panelsWorld->getById(newParentId);
        Panda::HierarchyCommand command(entities, parent);
        m_panelsWorld->getCommandHistory().SAVE(command, true);
    } else {
        for (Panda::Entity ent : entities) { m_panelsWorld->setParent(ent, {}); } // a drop onto the "Scene" root
    }
    m_panelsWorld->setChanged();
    refreshFromWorld();
}

/* --------------------------- Entity editor outputs --------------------------- */

void EditorController::entityDidEditName(const std::string &name) {
    std::vector<Panda::Entity> entities = selectedEntities();
    if (entities.empty()) { return; }
    for (Panda::Entity e : entities) { e.setName(name); } // setName calls setWorldChanged
    m_hierarchyView->reload();                            // the name is visible in the tree
    if (entities.size() == 1) { m_hierarchyView->setStatusText("Selected: " + entities.front().getName()); }
}

void EditorController::entityDidSetStatic(bool isStatic) {
    if (!m_panelsWorld) { return; }
    const std::vector<Panda::Entity> selected = selectedEntities();
    if (selected.empty()) { return; }
    const std::unordered_set<Panda::Entity> selection(selected.begin(), selected.end());
    const std::unordered_set<Panda::Entity> subtree = collectSubtree(*m_panelsWorld, selected);
    const auto apply = [this, isStatic](const std::unordered_set<Panda::Entity> &targets) {
        toggleComponentPresence<Panda::IsStaticComponent>(*m_panelsWorld, targets, isStatic);
    };
    const auto refresh = [this] { pushSelectionToPanels(); }; // the header rereads the flag (also after Cancel)
    PandaUI::Context *context = m_root ? m_root->getContext() : nullptr;
    if (subtree.size() == selection.size() || !context) {
        apply(selection);
        refresh();
        return;
    }
    showApplyToChildrenDialog(
        context, isStatic ? "Enable Static for children too?" : "Disable Static for children too?",
        "The selection has child objects. Apply the change to them as well?",
        [apply, selection, subtree](bool includeChildren) { apply(includeChildren ? subtree : selection); }, refresh
    );
}

void EditorController::commitTransformEdit(const std::function<void(Panda::Entity)> &apply, bool scaleEdit) {
    if (!m_panelsWorld) { return; }
    std::unordered_set<Panda::Entity> targets;
    for (Panda::Entity e : selectedEntities()) { targets.insert(e); }
    if (targets.empty()) { return; }
    Panda::EntityTransformCommand command(targets);
    command.saveBeforeEdit();
    for (Panda::Entity e : targets) {
        apply(e);
        if (scaleEdit) { // the systems rebuild what depends on the scale (collider shapes)
            e.scaleUpdated();
        } else {
            e.transformUpdated(); // a teleport of the runtime state (bodies; the joints stay alive)
        }
    }
    command.saveAfterEdit();
    m_panelsWorld->getCommandHistory().SAVE(command, false);
    m_panelsWorld->setChanged();
    Panda::SelectionContext::updateValues();                                // median recompute → gizmo follows the Inspector edit
    m_lastTransformVersion = Panda::SelectionContext::getTransformVersion(); // own edit — no self-refresh of the fields (caret)
}

// Transform edit of a multi-selection: ONLY the changed axes are applied (a diff against primary — mixed
// axes emit the primary value), so as not to overwrite the axes that differ between entities.
void EditorController::entityDidEditPosition(glm::vec3 position) {
    Panda::Entity primary = primaryEntity();
    if (!primary) { return; }
    const glm::vec3 old = primary.getTransform().getPosition();
    commitTransformEdit([&](Panda::Entity e) {
        glm::vec3 p = e.getTransform().getPosition();
        if (position.x != old.x) { p.x = position.x; }
        if (position.y != old.y) { p.y = position.y; }
        if (position.z != old.z) { p.z = position.z; }
        e.getTransform().setPosition(p);
    });
}

void EditorController::entityDidEditRotation(glm::vec3 rotation) {
    Panda::Entity primary = primaryEntity();
    if (!primary) { return; }
    const glm::vec3 old = primary.getTransform().getRotationEuler();
    commitTransformEdit([&](Panda::Entity e) {
        glm::vec3 r = e.getTransform().getRotationEuler();
        if (rotation.x != old.x) { r.x = rotation.x; }
        if (rotation.y != old.y) { r.y = rotation.y; }
        if (rotation.z != old.z) { r.z = rotation.z; }
        e.getTransform().setRotationEuler(r);
    });
}

void EditorController::entityDidEditScale(glm::vec3 scale) {
    Panda::Entity primary = primaryEntity();
    if (!primary) { return; }
    const glm::vec3 old = primary.getTransform().getScale();
    commitTransformEdit(
        [&](Panda::Entity e) {
            glm::vec3 s = e.getTransform().getScale();
            if (scale.x != old.x) { s.x = scale.x; }
            if (scale.y != old.y) { s.y = scale.y; }
            if (scale.z != old.z) { s.z = scale.z; }
            e.getTransform().setScale(s);
        },
        true
    );
}

void EditorController::entityDidAddComponent(ComponentType type) {
    setComponentPresence(type, /*add*/ true);
}

void EditorController::entityDidRemoveComponent(ComponentType type) {
    setComponentPresence(type, /*add*/ false);
}

void EditorController::setComponentPresence(ComponentType type, bool add) {
    if (!m_panelsWorld) { return; }
    std::unordered_set<Panda::Entity> entities;
    for (Panda::Entity e : selectedEntities()) { entities.insert(e); }
    if (entities.empty()) { return; }
    switch (type) {
        case ComponentType::Camera: toggleComponentPresence<Panda::CameraComponent>(*m_panelsWorld, entities, add); break;
        case ComponentType::DirectionalLight: toggleComponentPresence<Panda::DirectionalLightComponent>(*m_panelsWorld, entities, add); break;
        case ComponentType::PointLight: toggleComponentPresence<Panda::PointLightComponent>(*m_panelsWorld, entities, add); break;
        case ComponentType::SpriteRenderer: toggleComponentPresence<Panda::SpriteRendererComponent>(*m_panelsWorld, entities, add); break;
        case ComponentType::RuntimeMesh: toggleComponentPresence<Panda::RuntimeMeshComponent>(*m_panelsWorld, entities, add); break;
        case ComponentType::PrimitiveMesh: toggleComponentPresence<Panda::PrimitiveMeshComponent>(*m_panelsWorld, entities, add); break;
        case ComponentType::ImportedSubmesh: toggleComponentPresence<Panda::ImportedSubmeshComponent>(*m_panelsWorld, entities, add); break;
        case ComponentType::Sky: toggleComponentPresence<Panda::SkyComponent>(*m_panelsWorld, entities, add); break;
        case ComponentType::Script: return; // Script add/remove goes through addScript/removeScript
    }
    pushSelectionToPanels(); // the inspector rereads the entity → the section appears/disappears
}

void EditorController::entityDidEditComponent(const ComponentEdit &edit) {
    if (!m_panelsWorld) { return; }
    std::visit(
        [this](const auto &component) {
            using T = std::decay_t<decltype(component)>;
            std::unordered_set<Panda::Entity> targets;
            for (Panda::Entity e : selectedEntities()) {
                if (e.hasComponent<T>()) { targets.insert(e); }
            }
            commitComponent<T>(*m_panelsWorld, targets, component);
        },
        edit
    );
}

void EditorController::entityDidAddExtensionComponent(const std::string &component) {
    const ComponentInspector *inspector = editorExtensions().componentInspectors.find(component);
    if (!m_panelsWorld || inspector == nullptr || inspector->setPresence == nullptr) { return; }
    inspector->setPresence(*m_panelsWorld, selectedEntities(), /*add*/ true);
    pushSelectionToPanels(); // the inspector rereads the entity → the section appears
}

void EditorController::entityDidRemoveExtensionComponent(const std::string &component) {
    const ComponentInspector *inspector = editorExtensions().componentInspectors.find(component);
    if (!m_panelsWorld || inspector == nullptr || inspector->setPresence == nullptr) { return; }
    inspector->setPresence(*m_panelsWorld, selectedEntities(), /*add*/ false);
    pushSelectionToPanels();
}

void EditorController::entityDidSelectAsset(Panda::AssetId assetId) {
    if (assetId == Panda::AssetId(0)) { return; }
    selectAssets({assetId}, /*clearContentSelection*/ true); // opens/replaces the asset tab
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
    Panda::AssetHandlerEditor *handler = assetHandler();
    if (!handler) { return {}; }
    auto it = handler->getRegistry().find(id);
    if (it == handler->getRegistry().end()) { return {}; }
    return handler->getAssetName(it->first);
}

Panda::CommandHistory &EditorController::assetUndoStack(Panda::AssetId id) {
    if (auto *handler = assetHandler()) { return handler->documents().undoStackFor(id); }
    static Panda::CommandHistory orphan; // no project — the command has nowhere to go (no forms without a project)
    return orphan;
}

void EditorController::clearAssetUndoStack(Panda::AssetId id) {
    if (auto *handler = assetHandler()) { handler->documents().clearUndoStack(id); }
}

void EditorController::selectAsset(Panda::AssetId id) {
    entityDidSelectAsset(id);
    revealAsset(id); // the selection is shown where the asset lives too
}

void EditorController::revealAsset(Panda::AssetId id) {
    if (id == Panda::AssetId(0)) { return; }
    if (m_dockManager) { m_dockManager->showPanel("contentBrowser"); }
    m_contentView->revealAsset(id);
}

void EditorController::revealEntity(Panda::UUID id) {
    if (!id) { return; }
    if (m_dockManager) { m_dockManager->showPanel("hierarchy"); }
    m_hierarchyView->flashEntity(id);
}

void EditorController::selectEntity(Panda::UUID id) {
    if (id) { selectEntitiesByIds({id}); } // the Hierarchy reveals a single selection itself
}

PandaUI::TextureHandle EditorController::assetThumbnail(Panda::AssetId id) {
    auto *thumbnails = Panda::ThumbnailsManager::shared();
    return thumbnails ? thumbnails->thumbnailFor(id) : PandaUI::TextureHandle{};
}

PandaUI::FontFamily EditorController::assetFont(Panda::AssetId id) {
    return m_bridge ? m_bridge->fontFamilyForAsset(static_cast<uint32_t>(id)) : PandaUI::FontFamily::Sans;
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

void EditorController::log(Editor::HostLogLevel level, const std::string &message) {
    if (!m_outputView) { return; }
    OutputPanelView::LogLevel mapped = OutputPanelView::LogLevel::Info;
    switch (level) {
        case Editor::HostLogLevel::Info: mapped = OutputPanelView::LogLevel::Info; break;
        case Editor::HostLogLevel::Warning: mapped = OutputPanelView::LogLevel::Warning; break;
        case Editor::HostLogLevel::Error: mapped = OutputPanelView::LogLevel::Error; break;
    }
    m_outputView->appendLog(mapped, message);
}

void EditorController::entityDidAddScript(const std::string &className) {
    if (!m_panelsWorld) { return; }
    Panda::ScriptEngine *engine = Panda::GameContext::getScriptEngine();
    if (!engine || !engine->isLoaded()) { return; }
    const Panda::ScriptClassManifest clazz = engine->getManifest().getClass(className.c_str());
    if (!clazz) { return; }
    for (Panda::Entity we : selectedEntities()) { // the script goes to all selected entities
        if (!we.hasComponent<Panda::ScriptListComponent>()) { we.addComponent<Panda::ScriptListComponent>(); }
        std::vector<Panda::ScriptField> fields;
        for (const Panda::ScriptFieldManifest &fm : clazz.fields) {
            fields.emplace_back(0u, fm.handle, fm.name, fm.type, defaultScriptValue(fm.type));
        }
        we.addScript(Panda::ExternalScript(static_cast<Panda::Handle>(we.getId()), 0u, clazz.name, fields, clazz.order));
    }
    pushSelectionToPanels(); // a Script section with the real fields appears
}

void EditorController::entityDidRemoveScript(int scriptIndex) {
    Panda::Entity primary = primaryEntity();
    if (primary && primary.hasComponent<Panda::ScriptListComponent>()) {
        std::vector<Panda::ExternalScript> &scripts = primary.getComponent<Panda::ScriptListComponent>().scripts;
        if (scriptIndex >= 0 && scriptIndex < static_cast<int>(scripts.size())) {
            scripts.erase(scripts.begin() + scriptIndex); // script ids = 0 outside the runtime — remove by index
            primary.setWorldChanged();
        }
    }
    pushSelectionToPanels();
}

void EditorController::entityDidEditScriptField(int scriptIndex, const std::string &fieldName,
                                                   Panda::ScriptFieldValue value) {
    Panda::Entity primary = primaryEntity();
    if (!m_panelsWorld || !primary || !primary.hasComponent<Panda::ScriptListComponent>()) { return; }
    Panda::ScriptListComponent &slc = primary.getComponent<Panda::ScriptListComponent>();
    if (scriptIndex < 0 || scriptIndex >= static_cast<int>(slc.scripts.size())) { return; }
    Panda::ExternalScript &es = slc.scripts[scriptIndex];
    if (!es.hasField(fieldName.c_str())) { return; }
    // The command writes the value: into the world and, in Play, into the running script — the
    // same on undo and redo.
    Panda::EditScriptFieldCommand command(
        primary, static_cast<size_t>(scriptIndex), fieldName, es.getField(fieldName.c_str()).value, std::move(value)
    );
    m_panelsWorld->getCommandHistory().SAVE(command, true);
}

void EditorController::entityDidClickBackground() {
    deselectAll();
}

/* ---------------------------- Viewport output ---------------------------- */

void EditorController::viewportDidClickBackground() {
    deselectAll();
}

void EditorController::viewportDidRectSelect(PandaUI::Point start, PandaUI::Point current, bool additive, bool committed) {
    if (m_bridge) {
        m_bridge->rectSelect(start, current, additive, committed);
    } else {
        viewportDidClickBackground();
    }
}

void EditorController::viewportDidPickGizmo(int mode) {
    if (m_bridge) { m_bridge->setGizmoOperation(mode); }
}

void EditorController::viewportDidResetCamera() {
    if (m_bridge) { m_bridge->resetCamera(); } // camera → origin (through EditorLayer)
}

void EditorController::viewportDidPickSpace(bool worldSpace) {
    if (m_bridge) { m_bridge->setGizmoSpace(worldSpace); }
}

void EditorController::viewportDidDropAssets(const std::vector<std::uint32_t> &assetIds, const std::string &kind) {
    if (kind == "mesh") {
        std::vector<Panda::AssetId> ids;
        ids.reserve(assetIds.size());
        for (std::uint32_t id : assetIds) { ids.push_back(Panda::AssetId(id)); }
        instantiateMeshAssets(ids);
        return;
    }
    m_outputView->appendLog(
        OutputPanelView::LogLevel::Warning, "Drop of \"" + kind + "\" assets is not supported yet"
    );
}

// gltf mesh → an entity subtree (one root per asset): the engine builds the nodes/transforms/submeshes
// with materials.
void EditorController::instantiateMeshAssets(const std::vector<Panda::AssetId> &ids) {
    auto *handler = assetHandler();
    if (!handler || !m_panelsWorld || ids.empty()) { return; }
    std::vector<Panda::UUID> createdIds;
    const auto &registry = handler->getRegistry();
    for (Panda::AssetId id : ids) {
        auto it = registry.find(id);
        if (it == registry.end() || it->second.type != Panda::AssetType::IMPORTED_MESH) { continue; }
        const Panda::UUID rootId = Panda::AssetImporterBase::addGltfSceneToWorld(
            handler->getProjectPath() / handler->assetPath(id),
            Panda::AssetRef<Panda::ImportedMeshAsset>(id),
            *m_panelsWorld
        );
        Panda::Entity root = m_panelsWorld->getById(rootId);
        if (!root.isValid()) { continue; }
        Panda::AddRemoveEntitiesCommand command({root});
        m_panelsWorld->getCommandHistory().SAVE(command, false); // subtree already created — the command is for undo
        createdIds.push_back(rootId);
    }
    if (createdIds.empty()) { return; }
    m_panelsWorld->setChanged();
    refreshFromWorld();
    selectEntitiesByIds(createdIds);
    m_outputView->appendLog(
        OutputPanelView::LogLevel::Info,
        "Added " + std::to_string(createdIds.size()) + (createdIds.size() == 1 ? " mesh" : " meshes") + " to scene"
    );
}

/* ------------------------------ Content browser: the asset domain ------------------------------ */

void EditorController::selectAssets(const std::vector<Panda::AssetId> &ids, bool clearContentSelection) {
    // An empty selection while it is already empty is not a change (a click on a FOLDER is filtered to empty):
    // do not re-trigger the panels, otherwise with an entity selected pushSelectionToPanels would remove
    // the folder highlight that has just been set.
    if (ids.empty() && m_selectedAssets.empty()) { return; }
    m_selectedAssets = ids;
    if (!ids.empty()) { Panda::SelectionContext::unselectAll(); } // an asset selection clears the entity selection
    if (clearContentSelection) { m_contentView->clearAssetSelection(); }
    pushSelectionToPanels();
}

// Dedup: asset deletion (confirm → removeAsset → refresh) is common to both panels.
void EditorController::deleteAssets(const std::vector<Panda::AssetId> &ids) {
    auto *handler = assetHandler();
    if (!handler || ids.empty()) { return; }
    // File = asset: deletion erases the files from disk (otherwise a rescan would resurrect the asset).
    const std::string title = ids.size() == 1 ? "Delete \"" + handler->getAssetName(ids.front()) + "\"?"
                                              : "Delete " + std::to_string(ids.size()) + " assets?";
    showConfirmDialog(
        m_root ? m_root->getContext() : nullptr, title, "Files will be removed from disk. This cannot be undone.",
        "Delete", /*destructive*/ true,
        [this, ids] {
            auto *assets = assetHandler();
            if (!assets) { return; }
            for (Panda::AssetId id : ids) { assets->removeAsset(id); }
            m_selectedAssets.clear();
            refreshAssetPanels();
            pushSelectionToPanels();
            m_outputView->appendLog(
                OutputPanelView::LogLevel::Info, "Deleted " + std::to_string(ids.size()) + " assets"
            );
        }
    );
}

void EditorController::contentDidSelectAssets(const std::vector<Panda::AssetId> &assets) {
    selectAssets(assets, false);
}

void EditorController::contentDidRequestDelete(const std::vector<Panda::AssetId> &assets) {
    deleteAssets(assets);
}

void EditorController::contentAddFileItems(Menu &menu, Panda::AssetId asset) {
    auto *handler = assetHandler();
    if (!handler) { return; }
    const Panda::path_t relative = handler->assetPath(asset);
    if (relative.empty()) { return; }
    addFileItems(menu, handler->getProjectPath() / relative);
    // The id a world or a material references the asset by (hand edits of the text files).
    const std::string id = std::to_string(static_cast<uint32_t>(asset));
    menu.addItem("Copy ID: " + id, [id] { setClipboardText(id); });
    menu.addSeparator();
}

void EditorController::contentDidRequestImport(const std::vector<Panda::AssetId> &assets) {
    auto *handler = assetHandler();
    if (!handler) { return; }
    for (Panda::AssetId id : assets) {
        // Reimport of a world is a reread from disk through the engine if it is currently open:
        // a regular reload is a no-op for a world (the world lives in ProjectLoader, not in the
        // asset loading registry).
        if (handler->getInfo(id).type == Panda::AssetType::WORLD) {
            if (m_bridge) { m_bridge->reloadWorldAsset(id); }
            continue;
        }
        handler->reload(id);
    }
    refreshAssetPanels();
    m_outputView->appendLog(OutputPanelView::LogLevel::Info, "Reimported " + std::to_string(assets.size()) + " assets");
}

void EditorController::contentDidRequestRefresh() {
    // Tree rescan: new files become assets, the ids of existing ones are stable (sidecars).
    if (auto *handler = assetHandler()) { handler->rescanProject(); }
    refreshAssetPanels();
}

void EditorController::contentDidRequestAddToScene(const std::vector<Panda::AssetId> &assets) {
    instantiateMeshAssets(assets);
}

void EditorController::contentDidRequestOpenWorld(Panda::AssetId assetId) {
    if (m_bridge) { m_bridge->openWorldAsset(assetId); }
}

CreateActions EditorController::contentCreateActions(const Panda::path_t &directory) {
    CreateActions actions;
    // A material starts with the first built-in shader; the inspector changes it.
    actions.push_back({"Material", [this, directory] {
                           createMaterial(directory, builtinShaderOptions().front().first);
                       }});
    actions.push_back({"Postprocessing", [this, directory] { createPostprocessing(directory); }});
    actions.push_back({"Postprocess Shader", [this, directory] { createPostprocessShader(directory); }});
    // The assets of the modules; the registry does not change while the project is open.
    for (const AssetCreator &creator : editorExtensions().assetCreators) {
        const AssetCreator *creatorPtr = &creator;
        actions.push_back({creator.title, [this, directory, creatorPtr] { createAsset(directory, *creatorPtr); }});
    }
    return actions;
}

void EditorController::createMaterial(const Panda::path_t &directory, Panda::AssetId shaderId) {
    auto *handler = assetHandler();
    if (!handler) { return; }
    const Panda::AssetId created = handler->documents().createMaterialAsset(directory, shaderId);
    if (!created) {
        m_outputView->appendLog(OutputPanelView::LogLevel::Error, "Failed to create material");
        return;
    }
    refreshAssetPanels();
    selectAssets({created}, false); // open the form of the new material in the inspector right away
    m_outputView->appendLog(OutputPanelView::LogLevel::Info, "Created material");
}

void EditorController::createPostprocessing(const Panda::path_t &directory) {
    auto *handler = assetHandler();
    if (!handler) { return; }
    const Panda::AssetId created = handler->documents().createPostprocessingAsset(directory);
    if (!created) {
        m_outputView->appendLog(OutputPanelView::LogLevel::Error, "Failed to create postprocessing");
        return;
    }
    refreshAssetPanels();
    selectAssets({created}, false);
    m_outputView->appendLog(OutputPanelView::LogLevel::Info, "Created postprocessing");
}

void EditorController::createAsset(const Panda::path_t &directory, const AssetCreator &creator) {
    auto *handler = assetHandler();
    if (!handler || creator.create == nullptr) { return; }
    // The file is written by the contribution (EditorExtensions::assetCreators), registration and
    // selection are common.
    const Panda::path_t path = creator.create(directory);
    const Panda::AssetId created = path.empty() ? Panda::AssetId(0) : handler->documents().registerCreatedAsset(path);
    if (!created) {
        m_outputView->appendLog(OutputPanelView::LogLevel::Error, "Failed to create " + creator.title);
        return;
    }
    refreshAssetPanels();
    selectAssets({created}, false);
    m_outputView->appendLog(OutputPanelView::LogLevel::Info, "Created " + path.filename().string());
}

void EditorController::contentDidRequestRename(Panda::AssetId assetId, const std::string &newName) {
    auto *handler = assetHandler();
    if (!handler) { return; }
    if (!handler->renameAsset(assetId, newName)) {
        m_outputView->appendLog(OutputPanelView::LogLevel::Error, "Failed to rename asset");
        return;
    }
    refreshAssetPanels();
    pushSelectionToPanels(); // the name in the inspector/status may have changed
}

void EditorController::createPostprocessShader(const Panda::path_t &directory) {
    auto *handler = assetHandler();
    if (!handler) { return; }
    if (!handler->documents().createPostprocessShader(directory)) {
        m_outputView->appendLog(OutputPanelView::LogLevel::Error, "Failed to create postprocess shader");
        return;
    }
    refreshAssetPanels();
    m_outputView->appendLog(OutputPanelView::LogLevel::Info, "Created postprocess shader");
}

/* ----------------------------- Toolbar output ---------------------------- */

void EditorController::toolbarDidPickSceneState(SceneState state) {
    if (m_bridge) { m_bridge->setSceneState(state); } // the actual play/stop
    const char *message = state == SceneState::Play ? "Entered Play mode" : "Returned to Edit mode";
    m_outputView->appendLog(OutputPanelView::LogLevel::Info, message);
}

void EditorController::toolbarDidTapReload() {
    if (m_bridge) { m_bridge->reloadFromDisk(); } // assets/scripts/world by mtime
    m_outputView->appendLog(OutputPanelView::LogLevel::Info, "Reloading from disk…");
}

void EditorController::toolbarDidSave() {
    if (m_bridge) { m_bridge->save(); } // every unsaved file document (EditorLayer::saveAll)
}

/* ------------------------------ CMake panel ------------------------------ */

void EditorController::cmakeDidRequestScriptsBuild(Panda::BuildAction action, Panda::ScriptsBuildConfig config) {
    if (m_bridge) { m_bridge->startScriptsBuild(action, config); }
}

void EditorController::cmakeDidRequestExportBuild(Panda::BuildAction action, Panda::ExportProjectTarget target) {
    if (m_bridge) { m_bridge->startExportBuild(action, target); }
}

void EditorController::cmakeDidTapStop() {
    if (m_bridge) { m_bridge->cancelProjectBuild(); }
}

void EditorController::cmakeDidRequestShowProjectFolder(Panda::BuildTargetKind kind) {
    if (m_bridge) { m_bridge->showProjectFolder(kind); }
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
    if (!context || !anchor || !m_bridge) { return; }
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
    PND_ASSERT(m_bridge != nullptr, "SETTINGS STORES NEED THE ENGINE BRIDGE");
    return m_bridge->settingsStore(scope);
}

void EditorController::commitSettings(Panda::SettingsScope scope, Foundation::Shared<Panda::WorldCommand> command) {
    if (!command) { return; }
    switch (scope) {
        case Panda::SettingsScope::World: {
            // An undo step of the world: the sidebar edits dirty the world like any edit.
            if (!m_panelsWorld) { return; }
            m_panelsWorld->getCommandHistory().SAVE_SHARED(std::move(command), true);
            m_panelsWorld->setChanged();
            return;
        }
        case Panda::SettingsScope::Project: {
            // An undo step of the project settings document (its tab is the only place that edits
            // the scope); without the document — applied as is.
            auto *document = m_editorTabs
                                 ? dynamic_cast<ProjectSettingsDocument *>(m_editorTabs->documentOfType(PROJECT_SETTINGS_DOCUMENT))
                                 : nullptr;
            if (document == nullptr) {
                command->execute();
                return;
            }
            document->undoStack()->SAVE_SHARED(std::move(command), true);
            document->markDirty();
            return;
        }
        case Panda::SettingsScope::Editor: {
            // Applied and saved at once: no dirty state, no undo.
            command->execute();
            if (m_bridge) { m_bridge->editorSettingsChanged(); }
            return;
        }
    }
}

void EditorController::openPlugins() {
    if (m_editorTabs) { m_editorTabs->openDocumentType(PLUGINS_DOCUMENT, Dock::Center); }
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
        case Panda::SettingsScope::Project:
        case Panda::SettingsScope::Editor: {
            if (!m_editorTabs) { return; }
            const char *typeId = scope == Panda::SettingsScope::Project ? PROJECT_SETTINGS_DOCUMENT : EDITOR_SETTINGS_DOCUMENT;
            m_editorTabs->openDocumentType(typeId, Dock::Center);
            if (section.empty()) { return; }
            Document *document = m_editorTabs->documentOfType(typeId);
            if (auto *project = dynamic_cast<ProjectSettingsDocument *>(document)) { project->revealSection(section); }
            if (auto *editor = dynamic_cast<EditorSettingsDocument *>(document)) { editor->revealSection(section); }
            return;
        }
    }
}

void EditorController::appendCMakeOutput(Panda::BuildTargetKind kind, const std::vector<std::string> &lines) {
    m_cmakeView->appendLines(kind, lines);
}

void EditorController::cmakeBuildStarted(Panda::BuildTargetKind kind) {
    m_cmakeView->buildStarted(kind);
}

void EditorController::cmakeBuildFinished(Panda::BuildTargetKind kind, bool success, bool canceled) {
    m_cmakeView->buildFinished(kind, success, canceled);
}

void EditorController::toolbarDidExport() {
    m_outputView->appendLog(OutputPanelView::LogLevel::Info, "Export / Cook requested");
}

// Undo/redo go by the stack of the ACTIVE DOCUMENT (the document model): a document tab —
// its stack (an entity tab's is the world's), the viewport session — the document of the brush,
// otherwise the editing world. Called both by the toolbar and by the menu — the same semantics
// regardless of the UI.
Document *EditorController::activeDocument() {
    // The focus moves with every click (Context) — "where the user is" is reliable. The selected
    // document tab of the dock with the focus — its document; a side dock without one (Hierarchy)
    // — the world.
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
            m_lastUndoDocument = activeWorldDocument();
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
        return m_lastUndoDocument ? m_lastUndoDocument : activeWorldDocument();
    }
    m_lastUndoDocument = activeWorldDocument();
    return m_lastUndoDocument;
}

void EditorController::undo() {
    Document *document = activeDocument();
    Panda::CommandHistory *stack = document ? document->undoStack() : nullptr;
    if (!stack || !stack->CAN_UNDO()) {
        m_outputView->appendLog(OutputPanelView::LogLevel::Warning, "Undo — nothing to undo");
        return;
    }
    stack->UNDO();
    // The world stack (the viewport, the entity tabs): the panels reread the world; a document of
    // its own: its form rereads the document.
    Document *world = activeWorldDocument();
    if (world && m_panelsWorld && stack == &m_panelsWorld->getCommandHistory()) {
        world->refresh();
    } else {
        document->refresh();
    }
}

void EditorController::redo() {
    Document *document = activeDocument();
    Panda::CommandHistory *stack = document ? document->undoStack() : nullptr;
    if (!stack || !stack->CAN_REDO()) {
        m_outputView->appendLog(OutputPanelView::LogLevel::Warning, "Redo — nothing to redo");
        return;
    }
    stack->REDO();
    Document *world = activeWorldDocument();
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
    if (m_bridge) { m_bridge->updateIndicatorAction(); }
}

void EditorController::toolbarDidDismissUpdate() {
    if (m_bridge) { m_bridge->updateIndicatorDismiss(); }
}

void EditorController::toolbarDidNavigateBack() {
    if (m_editorTabs) { m_editorTabs->goBack(); }
}

void EditorController::toolbarDidNavigateForward() {
    if (m_editorTabs) { m_editorTabs->goForward(); }
}

/* ------------------------------ Docking output --------------------------- */

void EditorController::hierarchyDidRequestMaximize() {
    m_dockManager->maximizeContaining(m_hierarchyView.get());
}

void EditorController::entityDidRequestMaximize() {
    m_dockManager->maximize(Dock::Right); // fallback: tabs zoom THEIR OWN dock through tabsRequestMaximize
}

/* ----------------------------- Shortcuts --------------------------------- */

void EditorController::registerShortcuts() {
    m_shortcuts.registerCommand(
        "dock.toggleFullscreen", "Toggle Fullscreen",
        KeyChord{Key::Space, /*shift*/ true, /*ctrl*/ false, /*alt*/ false, /*super*/ false},
        [this] { return maximizeFocusedDock(); }
    );
    m_shortcuts.registerCommand(
        "camera.focusSelection", "Focus Selection",
        KeyChord{Key::Z, /*shift*/ false, /*ctrl*/ false, /*alt*/ false, /*super*/ false},
        [this] { if (m_bridge) { m_bridge->focusSelection(); } return true; }
    );
}

bool EditorController::handleShortcut(const KeyEvent &event) {
    return m_shortcuts.handleKey(event);
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

void EditorController::refreshStatistics() {
    int total = 0;
    if (m_panelsWorld) {
        m_panelsWorld->getAllEntitiesWith<Panda::IdComponent>().each([&total](Panda::IdComponent &) { ++total; });
    }
    m_statisticsView->setEntityCount(total);
}

/* ------------------------------ EditorTabsHost ---------------------------- */

EntityEditorOutput &EditorController::tabsHostOutput() {
    return *this;
}

void EditorController::tabsBeginEntityTarget(const std::vector<Panda::UUID> &ids) {
    m_entityTargetOverride = ids;
    m_entityTargetActive = true;
}

void EditorController::tabsEndEntityTarget() {
    m_entityTargetActive = false;
    m_entityTargetOverride.clear();
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

void EditorController::tabsRequestMaximize(PandaUI::View *view) {
    m_dockManager->maximizeContaining(view);
}

void EditorController::tabsAssetTabOpened(Panda::AssetId id) {
    if (auto *handler = assetHandler()) { handler->setAssetOpenedInTab(id, true); }
}

void EditorController::tabsAssetTabClosed(Panda::AssetId id) {
    if (auto *handler = assetHandler()) { handler->setAssetOpenedInTab(id, false); }
    // A viewport session without the tab of its document is an invisible LMB capture: the tool lets go.
    if (m_viewportEditingDocument == id) { viewportDidRequestStopEditing(); }
}

Panda::World *EditorController::tabsWorld() {
    return m_panelsWorld;
}

bool EditorController::tabsSaveWorld() {
    Document *world = activeWorldDocument(); // an entity tab is a view of the active world
    return world && world->save();
}

bool EditorController::tabsSaveAsset(Panda::AssetId id) {
    return m_bridge && m_bridge->saveAsset(id);
}

std::filesystem::path EditorController::tabsWorldFilePath() {
    Document *world = activeWorldDocument();
    return world ? world->filePath() : std::filesystem::path();
}

void EditorController::tabsDocumentClosed(Document *document) {
    if (m_lastUndoDocument == document) { m_lastUndoDocument = nullptr; }
    if (auto *world = dynamic_cast<WorldDocument *>(document)) {
        // The viewport outlives the tab it lived in: the next sync moves it into the tab of the
        // world that is active then.
        if (m_viewportView && m_viewportView->getSuperview() == world->host().get()) {
            m_viewportView->removeFromSuperview();
        }
        // The user closed the tab: the world leaves the list of the project on the next sync,
        // not from inside the dock's close.
        if (!m_closingWorldTab) { m_closedWorldTabs.push_back(world->worldId()); }
    }
}

void EditorController::tabsApplySelection(
    const std::vector<Panda::UUID> &entities, const std::vector<Panda::AssetId> &assets
) {
    // The regular selection paths: Hierarchy/highlight/tab follow as on an ordinary click.
    if (!entities.empty()) {
        selectEntitiesByIds(entities);
    } else if (!assets.empty()) {
        selectAssets(assets, /*clearContentSelection*/ true);
    }
}

void EditorController::tabsHistoryChanged(bool canGoBack, bool canGoForward) {
    if (m_toolbarView) { m_toolbarView->setCanNavigate(canGoBack, canGoForward); }
}

} // namespace Editor
