#include "UI/EditorController.hpp"

#include <Foundation/GraphicsAPI.hpp>

#include "Preview/ThumbnailsManager.hpp"
#include "UI/Documents/CoreAssetEditors.hpp"
#include "UI/Documents/PluginsDocument.hpp"
#include "UI/Documents/SettingsDocuments.hpp"
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

} // namespace

// The world document: the viewport tab. Dirty is the world's change flag, Save goes through the
// host (the world and every dirty asset), the undo stack is the world's, a refresh after undo/redo
// rereads the panels and the settings sidebar.
class WorldDocument final : public Document {
public:
    explicit WorldDocument(EditorController *controller)
        : m_controller(controller) {}

    std::string title() override {
        return "Viewport";
    }
    std::shared_ptr<PandaUI::View> view() override {
        return m_controller->m_viewportView;
    }
    bool isDirty() override {
        return m_controller->m_panelsWorld && m_controller->m_panelsWorld->isChanged();
    }
    bool save() override {
        if (m_controller->m_bridge) { m_controller->m_bridge->save(); }
        return true;
    }
    Panda::CommandHistory *undoStack() override {
        return m_controller->m_panelsWorld ? &m_controller->m_panelsWorld->getCommandHistory() : nullptr;
    }
    void refresh() override {
        if (m_controller->m_panelsWorld) { m_controller->m_panelsWorld->setChanged(); }
        m_controller->refreshFromWorld();
    }

private:
    EditorController *m_controller;
};

/* ------------------------------ Coordinator ------------------------------ */

EditorController::EditorController()
    : m_hostBinding(this)
    , m_hierarchyView(std::make_shared<HierarchyPanelView>(this))
    , m_contentView(std::make_shared<ContentBrowserView>(this))
    , m_assetManagerView(std::make_shared<AssetManagerPanelView>(this))
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
    m_worldDocument = std::make_unique<WorldDocument>(this);
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
        host.save = [] { return s_controller->m_bridge ? s_controller->m_bridge->saveProjectSettings() : false; };
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
            std::make_unique<EditorSettingsDocument>(s_controller->settings(Panda::SettingsScope::Editor))
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
}

Panda::DockLayoutState EditorController::captureDockLayout() const {
    Panda::DockLayoutState state = m_dockManager->captureLayout();
    state.worldSettingsVisible = m_viewportView && m_viewportView->isSidebarVisible();
    return state;
}

void EditorController::applyDockLayout(const Panda::DockLayoutState &state) {
    m_dockManager->applyLayout(state);
    // Pinned editor tabs: restored after the layout (the world is already set —
    // EditorLayer calls setWorld before applyDockLayout; dead targets are skipped).
    if (m_editorTabs) { m_editorTabs->restorePinned(state.pinnedTabs); }
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

void EditorController::refreshSettingsDocuments() {
    if (!m_editorTabs) { return; }
    for (const char *typeId : {PROJECT_SETTINGS_DOCUMENT, EDITOR_SETTINGS_DOCUMENT}) {
        if (Document *document = m_editorTabs->documentOfType(typeId)) { document->refresh(); }
    }
}

void EditorController::worldContextChanged() {
    m_lastUndoDocument = nullptr; // the undo target pointed into the previous context
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
    m_assetManagerView->refresh();
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
        m_assetManagerView->setSelectedAssetIds({});
        m_contentView->clearAssetSelection();
        m_hierarchyView->setStatusText(entities.size() == 1
                                           ? "Selected: " + entities.front().getName()
                                           : "Selected: " + std::to_string(entities.size()) + " items");
    } else if (!m_selectedAssets.empty()) {
        m_assetManagerView->setSelectedAssetIds(m_selectedAssets);
        m_hierarchyView->setSelectedEntities({});
        m_hierarchyView->setStatusText("Selected: -");
    } else {
        m_hierarchyView->setSelectedEntities({});
        m_assetManagerView->setSelectedAssetIds({});
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
    Panda::ScriptEngine *engine = Panda::GameContext::getScriptEngine();
    const bool scriptingReady = engine && engine->isLoaded();
    // The undo/redo buttons show the stack state of the ACTIVE document (just like Cmd+Z itself).
    Document *document = activeDocument();
    Panda::CommandHistory *stack = document ? document->undoStack() : nullptr;
    const bool canUndo = stack && stack->CAN_UNDO();
    const bool canRedo = stack && stack->CAN_REDO();
    const bool canPlay = isEditMode && scriptingReady; // Play+Simulate: only from Edit and with scripting loaded
    const bool canStop = !isEditMode;                  // Stop: only while the world is running

    if (canUndo != m_tbCanUndo) { m_tbCanUndo = canUndo; m_toolbarView->setCanUndo(canUndo); }
    if (canRedo != m_tbCanRedo) { m_tbCanRedo = canRedo; m_toolbarView->setCanRedo(canRedo); }
    if (canPlay != m_tbCanPlay) { m_tbCanPlay = canPlay; m_toolbarView->setCanPlay(canPlay); }
    if (canStop != m_tbCanStop) { m_tbCanStop = canStop; m_toolbarView->setCanStop(canStop); }
    if (isEditMode != m_tbCanReload) {
        m_tbCanReload = isEditMode;
        m_toolbarView->setCanReloadScripts(isEditMode);
        if (m_editorTabs) { m_editorTabs->setRuntimeMode(!isEditMode); } // "runtime world" banner in PLAY/SIMULATE
    }

    // The "unsaved" indicator on the scene tab (viewport) — by World::isChanged() (reset on save/load).
    const bool dirty = m_panelsWorld && m_panelsWorld->isChanged();
    if (dirty != m_lastWorldDirty) {
        m_lastWorldDirty = dirty;
        if (m_dockManager) { m_dockManager->setPanelDirty("viewport", dirty); }
    }
    // The "unsaved" dots on asset tabs: per tab, by isChanged of EXACTLY their assets
    // (the document model: tabs are the source of truth about what is unsaved).
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
    // The viewport is fixed: it is not hidden (not in the strip), has no close button and is not dragged
    // out of the center.
    panels.push_back({"viewport", "Viewport", Ic::PanelViewport, m_viewportView, Dock::Center, true, false, false});
    panels.push_back({"hierarchy", "Hierarchy", Ic::PanelHierarchy, m_hierarchyView, Dock::Left});
    // The Inspector panel is gone: the selection opens as transient tabs (EditorTabs) in the right dock.
    panels.push_back({"contentBrowser", "Content Browser", Ic::PanelContentBrowser, m_contentView, Dock::Bottom});
    panels.push_back({"output", "Output", Ic::PanelOutput, m_outputView, Dock::Bottom});
    panels.push_back({"cmake", "CMake", Ic::PanelCMake, m_cmakeView, Dock::Bottom});
    panels.push_back({"assetManager", "Asset Manager", Ic::PanelAssetManager, m_assetManagerView, Dock::Bottom});
    // Diagnostic panels do not occupy the visibility strip — they are added through the dock's "+".
    panels.push_back({"statistics", "Statistics", Ic::PanelStatistics, m_statisticsView, Dock::Bottom,
                      /*open*/ true, /*inStrip*/ false});
    panels.push_back({"profiler", "Profiler", Ic::PanelProfiler, m_profilerView, Dock::Bottom,
                      /*open*/ true, /*inStrip*/ false});

    m_dockManager = std::make_unique<DockManager>(m_toolbarView, centerTabs, std::move(panels));
    m_editorTabs = std::make_unique<EditorTabs>(m_dockManager.get(), this);
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

void EditorController::hierarchyDidRequestAddChild(Panda::UUID parentId) {
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

void EditorController::hierarchyDidRequestAddPrimitive(Panda::UUID parentId, PrimitiveKind kind) {
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

/* ------------------- Content browser + Asset manager (the shared asset domain) ------------------- */

// Dedup: an asset selection comes from two panels (Content / AssetManager) with the same semantics.
void EditorController::selectAssets(const std::vector<Panda::AssetId> &ids, bool clearContentSelection) {
    // An empty selection while it is already empty is not a change (a click on a FOLDER is filtered to empty):
    // do not re-trigger the panels, otherwise with an entity selected pushSelectionToPanels would remove
    // the folder highlight that has just been set.
    if (ids.empty() && m_selectedAssets.empty()) { return; }
    m_selectedAssets = ids;
    if (!ids.empty()) { Panda::SelectionContext::unselectAll(); } // an asset selection clears the entity selection
    if (clearContentSelection) { m_contentView->clearAssetSelection(); } // the source is AssetManager
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

void EditorController::contentDidRequestCreateMaterial(const Panda::path_t &directory, Panda::AssetId shaderId) {
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

void EditorController::contentDidRequestCreatePostprocessing(const Panda::path_t &directory) {
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

void EditorController::contentDidRequestCreateAsset(const Panda::path_t &directory, const AssetCreator &creator) {
    auto *handler = assetHandler();
    if (!handler || creator.create == nullptr) { return; }
    // The file is written by the contribution (EditorExtensions::assetCreators), registration and
    // selection are common.
    const Panda::path_t path = creator.create(directory);
    const Panda::AssetId created = path.empty() ? Panda::AssetId(0) : handler->documents().registerCreatedAsset(path);
    if (!created) {
        m_outputView->appendLog(OutputPanelView::LogLevel::Error, "Failed: " + creator.title);
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

void EditorController::contentDidRequestCreatePostprocessShader(const Panda::path_t &directory) {
    auto *handler = assetHandler();
    if (!handler) { return; }
    if (!handler->documents().createPostprocessShader(directory)) {
        m_outputView->appendLog(OutputPanelView::LogLevel::Error, "Failed to create postprocess shader");
        return;
    }
    refreshAssetPanels();
    m_outputView->appendLog(OutputPanelView::LogLevel::Info, "Created postprocess shader");
}

/* -------------------------- Asset manager output ------------------------- */

void EditorController::assetManagerDidSelectAssets(const std::vector<Panda::AssetId> &ids) {
    selectAssets(ids, true);
}

void EditorController::assetManagerDidRequestDelete(const std::vector<Panda::AssetId> &ids) {
    deleteAssets(ids);
}

void EditorController::assetManagerDidRequestOpenWorld(Panda::AssetId id) {
    if (m_bridge) { m_bridge->openWorldAsset(id); }
}

/* ----------------------------- Toolbar output ---------------------------- */

void EditorController::toolbarDidPickSceneState(SceneState state) {
    if (m_bridge) { m_bridge->setSceneState(state); } // the actual play/simulate/stop
    const char *message = state == SceneState::Play       ? "Entered Play mode"
                          : state == SceneState::Simulate ? "Entered Simulate mode"
                                                          : "Returned to Edit mode";
    m_outputView->appendLog(OutputPanelView::LogLevel::Info, message);
}

void EditorController::toolbarDidTapReload() {
    if (m_bridge) { m_bridge->reloadFromDisk(); } // assets/scripts/world by mtime
    m_outputView->appendLog(OutputPanelView::LogLevel::Info, "Reloading from disk…");
}

void EditorController::toolbarDidSave() {
    if (m_bridge) { m_bridge->save(); } // actual save through EditorLayer::saveWorld (it logs "World saved." on success)
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

std::vector<std::string> EditorController::dirtyProjectDocumentTitles() {
    std::vector<std::string> titles;
    if (!m_editorTabs) { return titles; }
    for (Document *document : m_editorTabs->dirtyDocuments()) {
        if (dynamic_cast<ProjectSettingsDocument *>(document)) { titles.push_back(document->title()); }
    }
    return titles;
}

bool EditorController::saveProjectDocuments() {
    if (!m_editorTabs) { return true; }
    bool ok = true;
    for (Document *document : m_editorTabs->dirtyDocuments()) {
        if (dynamic_cast<ProjectSettingsDocument *>(document) && !document->save()) { ok = false; }
    }
    return ok;
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
            m_lastUndoDocument = m_worldDocument.get();
            return m_worldDocument.get();
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
        return m_lastUndoDocument ? m_lastUndoDocument : m_worldDocument.get();
    }
    m_lastUndoDocument = m_worldDocument.get();
    return m_worldDocument.get();
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
    if (m_panelsWorld && stack == &m_panelsWorld->getCommandHistory()) {
        m_worldDocument->refresh();
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
    if (m_panelsWorld && stack == &m_panelsWorld->getCommandHistory()) {
        m_worldDocument->refresh();
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

void EditorController::tabsDocumentClosed(Document *document) {
    if (m_lastUndoDocument == document) { m_lastUndoDocument = nullptr; }
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
