#pragma once

#include "UI/Panels/ContentBrowserView.hpp"
#include "UI/Panels/AssetManagerPanel.hpp"
#include "UI/Panels/CMakePanel.hpp"
#include "UI/Panels/OutputPanel.hpp"
#include "UI/Panels/HierarchyPanel.hpp"
#include "UI/Panels/SettingsSectionsView.hpp"
#include "UI/Panels/StatisticsPanel.hpp"
#include "UI/Documents/EntityEditorView.hpp"
#include "UI/Panels/ProfilerPanel.hpp"
#include "UI/Panels/Toolbar.hpp"
#include "UI/Panels/ViewportPanel.hpp"
#include <PandaEditorKit/Widgets/EditorView.hpp>
#include "UI/DockManager.hpp"
#include "UI/EditorTabs.hpp"
#include "UI/Shortcuts.hpp"
#include "UI/EngineBridge.hpp"

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
} // namespace Panda

namespace Editor {

class WorldDocument;

// The editor COORDINATOR — the carrier of the logic. Implements the Output of all panels; the panels consume
// the engine DIRECTLY (entities by UUID from World, assets from the registry, entity selection —
// SelectionContext). There is no intermediate view-model: the coordinator only applies intents with engine
// commands and pushes updates to the panels. It is also the host of the editor parts of packages
// (Editor::EditorHost, PandaEditorKit) and the owner of the world document (the document model:
// the viewport tab with the world settings sidebar; the other documents live in EditorTabs).
class EditorController final : public Editor::EditorHost,
                               public HierarchyPanelOutput,
                               public EntityEditorOutput,
                               public ViewportPanelOutput,
                               public ContentBrowserOutput,
                               public AssetManagerOutput,
                               public ToolbarOutput,
                               public CMakePanelOutput,
                               public EditorTabsHost {
public:
    // Status indicators of the visibility strip (RAM/VRAM/CPU/GPU from EditorLayer).
    void setStripStatusText(const std::string &text);
    EditorController();
    ~EditorController() override; // unbinds the process host (Editor::setEditorHost(nullptr))

    std::shared_ptr<PandaUI::View> rootView() const {
        return m_root;
    }

    // Migration seam #1: engine pushes the live scene texture in and reads the viewport panel's frame out.
    void setViewportSceneTexture(PandaUI::TextureHandle texture, bool flip) {
        if (m_viewportView) { m_viewportView->setSceneTexture(texture, flip); }
    }
    PandaUI::Rect viewportSceneFrame() const {
        return m_viewportView ? m_viewportView->sceneWorldFrame() : PandaUI::Rect();
    }
    bool isViewportFocused() const {
        return m_viewportView && m_viewportView->isSceneFocused();
    }
    bool isViewportHovered() const {
        return m_viewportView && m_viewportView->isSceneHovered();
    }
    void setGizmoVisible(bool visible) {
        if (m_viewportView) { m_viewportView->setGizmoVisible(visible); }
    }
    void setGizmoActive(bool active) {
        if (m_viewportView) { m_viewportView->setGizmoActive(active); }
    }
    void setViewportGameInput(bool gameInput) { // PLAY: viewport clicks/keys go to the game (Alt+LMB — pick)
        if (m_viewportView) { m_viewportView->setGameInputMode(gameInput); }
    }
    void setViewportToolCapture(bool capture) { // a viewport tool takes over LMB (set by Layer every frame)
        if (m_viewportView) { m_viewportView->setToolCapture(capture); }
    }
    void setBridge(EngineBridge *bridge) { m_bridge = bridge; } // engine/host bridge (implemented by EditorLayer)

    void setWorld(Panda::World *world);       // project open: the panels' world + asset registry rescan
    void setActiveWorld(Panda::World *world); // Play/Simulate/Stop: the panels switch to the live world
    void syncWorldStructure(); // per frame: scripts change the world in PLAY → reload Hierarchy by version
    void thumbnailDidBecomeReady(Panda::AssetId assetId); // the thumbnail is ready → update the grid cell
    void refreshAssetPanels();          // content + asset manager reread the registry (project change)
    void setWorldDisplayName(const std::string &name);
    // The context has changed (another world / another project / close / new world): entity tabs point
    // into the old world — they are closed; the navigation history points to dead objects — it is cleared.
    void worldContextChanged(); // name of the opened world → the Hierarchy root
    // Per-frame render metrics of the current world (supplied by EditorLayer) → the Statistics panel.
    void updateProfilerPanel(); // rereads the Foundation::Profiler snapshot (called by EditorLayer)
    void setFrameStatistics(
        int fps, uint32_t drawCalls3d, uint32_t shadowDrawCalls, uint32_t drawCalls2d, uint32_t quads,
        uint32_t uiLayouts
    );
    void collectExtensionStatistics(); // per frame: statistics sections of the editor parts
    // The editor registries have gained entries (the editor parts are loaded when a project is opened):
    // the panels with contribution sections are rebuilt.
    void extensionsChanged();
    void undo();                        // undo on the active document's stack (asset tab/brush — asset, else world)
    void redo();
    void syncSelectionFromEngine();     // SelectionContext → panels (per frame; drives selection from the viewport)
    void updateChromeFromState(bool isEditMode); // per frame: toolbar enable/disable + scene tab dirty indicator
    void setUpdateStatus(const UpdateIndicatorModel &model); // the editor updater → the toolbar indicator
    // Project build (Layer → CMake panel): cmake output once per frame + the change of the running state.
    void appendCMakeOutput(Panda::BuildTargetKind kind, const std::vector<std::string> &lines);
    void cmakeBuildStarted(Panda::BuildTargetKind kind);
    void cmakeBuildFinished(Panda::BuildTargetKind kind, bool success, bool canceled);

    // The docking layout (persisted in the editor session through Layer).
    Panda::DockLayoutState captureDockLayout() const;
    void applyDockLayout(const Panda::DockLayoutState &state);

    // Viewport helper lines (recomputed by the engine once per frame).
    void setViewportHelperLines(std::vector<Panda::HelperLine> lines) {
        if (m_viewportView) { m_viewportView->setHelperLines(std::move(lines)); }
    }
    // The dirty documents of the project besides the world and the assets (the project settings):
    // the save flows of Layer (Cmd+S saves everything; the close questions name them).
    std::vector<std::string> dirtyProjectDocumentTitles();
    bool saveProjectDocuments();

    // HierarchyPanelOutput.
    void hierarchyDidSelectEntities(const std::vector<Panda::UUID> &ids) override;
    void hierarchyDidRequestAddChild(Panda::UUID parentId) override;
    void hierarchyDidRequestAddPrimitive(Panda::UUID parentId, PrimitiveKind kind) override;
    void hierarchyDidRequestDuplicate(const std::vector<Panda::UUID> &ids) override;
    void hierarchyDidRequestDelete(const std::vector<Panda::UUID> &ids) override;
    void hierarchyDidReparent(const std::vector<Panda::UUID> &items, Panda::UUID newParentId) override;
    void hierarchyDidRequestMaximize() override;

    // EntityEditorOutput.
    void entityDidEditName(const std::string &name) override;
    void entityDidSetStatic(bool isStatic) override;
    void entityDidEditPosition(glm::vec3 position) override;
    void entityDidEditRotation(glm::vec3 rotation) override;
    void entityDidEditScale(glm::vec3 scale) override;
    void entityDidAddComponent(ComponentType type) override;
    void entityDidRemoveComponent(ComponentType type) override;
    void entityDidEditComponent(const ComponentEdit &edit) override;
    void entityDidAddExtensionComponent(const std::string &component) override;
    void entityDidRemoveExtensionComponent(const std::string &component) override;
    void entityDidAddScript(const std::string &className) override;
    void entityDidRemoveScript(int scriptIndex) override;
    void entityDidEditScriptField(int scriptIndex, const std::string &fieldName, Panda::ScriptFieldValue value) override;
    void entityDidClickBackground() override;
    void entityDidSelectAsset(Panda::AssetId assetId) override;
    void entityDidRequestMaximize() override;

    // ViewportPanelOutput.
    void viewportDidClickBackground() override;
    void viewportDidRectSelect(PandaUI::Point start, PandaUI::Point current, bool additive, bool committed) override;
    void viewportDidPickGizmo(int mode) override;
    void viewportDidResetCamera() override;
    void viewportDidPickSpace(bool worldSpace) override;
    void viewportDidDropAssets(const std::vector<std::uint32_t> &assetIds, const std::string &kind) override;
    void viewportDidRequestDisplayOptions(PandaUI::View *anchor) override;
    void viewportDidToggleWorldSettings() override;
    void viewportDidRequestStopEditing() override;

    // Editor::EditorHost — the host of the editor parts of packages.
    std::string assetTitle(Panda::AssetId id) override;
    Panda::CommandHistory &assetUndoStack(Panda::AssetId id) override;
    void clearAssetUndoStack(Panda::AssetId id) override;
    void selectAsset(Panda::AssetId id) override;
    PandaUI::TextureHandle assetThumbnail(Panda::AssetId id) override;
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
    void refreshFromWorld() override; // rereads the panels after structural changes/undo
    void refreshAssetEditors(Panda::AssetId id) override;
    void log(Editor::HostLogLevel level, const std::string &message) override;
    Panda::TypeContext &settings(Panda::SettingsScope scope) override;
    void commitSettings(Panda::SettingsScope scope, Foundation::Shared<Panda::WorldCommand> command) override;
    void openSettings(Panda::SettingsScope scope, const std::string &section = {}) override;
    void restartEditor() override;

    // ContentBrowserOutput.
    void contentDidSelectAssets(const std::vector<Panda::AssetId> &assets) override;
    void contentDidRequestDelete(const std::vector<Panda::AssetId> &assets) override;
    void contentDidRequestImport(const std::vector<Panda::AssetId> &assets) override;
    void contentDidRequestRefresh() override;
    void contentDidRequestAddToScene(const std::vector<Panda::AssetId> &assets) override;
    void contentDidRequestOpenWorld(Panda::AssetId assetId) override;
    void contentDidRequestCreateMaterial(const Panda::path_t &directory, Panda::AssetId shaderId) override;
    void contentDidRequestCreatePostprocessing(const Panda::path_t &directory) override;
    void contentDidRequestCreatePostprocessShader(const Panda::path_t &directory) override;
    void contentDidRequestCreateAsset(const Panda::path_t &directory, const AssetCreator &creator) override;
    void contentDidRequestRename(Panda::AssetId assetId, const std::string &newName) override;

    // AssetManagerOutput.
    void assetManagerDidSelectAssets(const std::vector<Panda::AssetId> &ids) override;
    void assetManagerDidRequestDelete(const std::vector<Panda::AssetId> &ids) override;
    void assetManagerDidRequestOpenWorld(Panda::AssetId id) override;

    // ToolbarOutput.
    void toolbarDidPickSceneState(SceneState state) override;
    void toolbarDidTapReload() override;
    void toolbarDidSave() override;
    void toolbarDidExport() override;
    void toolbarDidUndo() override;
    void toolbarDidRedo() override;
    void toolbarDidNavigateBack() override;
    void toolbarDidNavigateForward() override;
    void toolbarDidActivateUpdate() override;
    void toolbarDidDismissUpdate() override;

    // CMakePanelOutput.
    void cmakeDidRequestScriptsBuild(Panda::BuildAction action, Panda::ScriptsBuildConfig config) override;
    void cmakeDidRequestExportBuild(Panda::BuildAction action, Panda::ExportProjectTarget target) override;
    void cmakeDidTapStop() override;
    void cmakeDidRequestShowProjectFolder(Panda::BuildTargetKind kind) override;

    bool handleShortcut(const PandaUI::KeyEvent &event);

    // EditorTabsHost: editor tabs edit THEIR OWN targets — for the duration of an edit the global
    // selection is substituted with the tab's targets (selectedEntities returns them).
    EntityEditorOutput &tabsHostOutput() override;
    void tabsBeginEntityTarget(const std::vector<Panda::UUID> &ids) override;
    void tabsEndEntityTarget() override;
    std::vector<Panda::Entity> tabsResolveEntities(const std::vector<Panda::UUID> &ids) override;
    std::vector<std::string> tabsScriptClasses() override;
    void tabsRequestMaximize(PandaUI::View *view) override;
    void tabsAssetTabClosed(Panda::AssetId id) override;
    void tabsAssetTabOpened(Panda::AssetId id) override;
    void tabsDocumentClosed(Document *document) override;
    void tabsApplySelection(
        const std::vector<Panda::UUID> &entities, const std::vector<Panda::AssetId> &assets
    ) override;
    void tabsHistoryChanged(bool canGoBack, bool canGoForward) override;

private:
    friend class WorldDocument;
    void buildLayout();
    void registerDocumentTypes(); // the settings of the project and of the editor in the "+" menu
    void registerShortcuts();
    bool maximizeFocusedDock();
    // The document that undo/redo is addressed to (the document model): the selected document tab
    // of the dock with the focus, the document of the active viewport session (brush strokes go
    // into its stack), else the world.
    Document *activeDocument();
    Document *m_lastUndoDocument = nullptr; // survives the focus loss caused by form rebuilds
    void showDisplayOptions(PandaUI::View *anchor); // the viewport section of the editor scope in a popover
    void setWorldSettingsVisible(bool visible);
    void refreshSettingsDocuments(); // the open settings documents reread their stores (module parts loaded)
    void pushSelectionToPanels();               // current selection (SelectionContext + m_selectedAssets) → panels
    std::vector<Panda::Entity> selectedEntities(); // valid selected entities (from SelectionContext)
    Panda::Entity primaryEntity();              // the first valid selected one (or invalid)
    void selectEntitiesByIds(const std::vector<Panda::UUID> &ids); // selection → SelectionContext + panels
    void deselectAll();
    // A transform edit through a command (undo, merge) + the systems' sync (Entity::transformUpdated /
    // scaleUpdated): scaleEdit — a rebuild of what depends on the scale, otherwise a teleport.
    void commitTransformEdit(const std::function<void(Panda::Entity)> &apply, bool scaleEdit = false);
    void setComponentPresence(ComponentType type, bool add); // add/remove of an engine component + command
    void selectAssets(const std::vector<Panda::AssetId> &ids, bool clearContentSelection); // asset selection dedup
    void deleteAssets(const std::vector<Panda::AssetId> &ids);  // asset deletion dedup (Content/AssetManager)
    void instantiateMeshAssets(const std::vector<Panda::AssetId> &ids); // gltf → entity subtree (D&D/menu)
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
    Panda::World *m_panelsWorld = nullptr; // the world shown by the panels (editing or playing)
    uint64_t m_lastStructureVersion = 0;   // snapshot of World::getStructureVersion for syncWorldStructure
    std::vector<Panda::AssetId> m_selectedAssets;        // asset selection (entities are in SelectionContext)
    std::uint64_t m_lastSelectionVersion = 0; // entity selection version at the last push (sync from the viewport)
    std::uint64_t m_lastTransformVersion = 0; // transform version at the last push/edit (gizmo↔Inspector sync)
    // Cache of the toolbar enable states (init = the buttons' default enabled) — pushed to the View only
    // on change.
    bool m_tbCanUndo = true, m_tbCanRedo = true, m_tbCanPlay = true, m_tbCanStop = true, m_tbCanReload = true;
    bool m_lastWorldDirty = false; // the last "unsaved" state pushed to the scene tab (World::isChanged)
    std::shared_ptr<HierarchyPanelView> m_hierarchyView;
    std::shared_ptr<ContentBrowserView> m_contentView;
    std::shared_ptr<AssetManagerPanelView> m_assetManagerView;
    std::shared_ptr<ToolbarPanelView> m_toolbarView;
    std::shared_ptr<OutputPanelView> m_outputView;
    std::shared_ptr<CMakePanelView> m_cmakeView;
    std::shared_ptr<StatisticsPanelView> m_statisticsView;
    std::shared_ptr<ProfilerPanelView> m_profilerView;
    std::shared_ptr<ViewportPanelView> m_viewportView;
    EngineBridge *m_bridge = nullptr; // requests to the engine/host (save/play/gizmo/rect-select)
    std::shared_ptr<PandaUI::View> m_root;
    std::unique_ptr<DockManager> m_dockManager;
    std::unique_ptr<EditorTabs> m_editorTabs; // editor tabs of the selection (after m_dockManager)
    // The world document: the viewport tab; its settings sidebar shows the sections of the world
    // scope over the context of the panels' world.
    std::unique_ptr<WorldDocument> m_worldDocument;
    std::shared_ptr<SettingsSectionsView> m_worldSettingsView;
    // The active target of an edit from a tab (RAII through tabsBegin/EndEntityTarget): selectedEntities
    // returns it instead of SelectionContext — edits of pinned tabs hit their own objects.
    std::vector<Panda::UUID> m_entityTargetOverride;
    bool m_entityTargetActive = false;
    // The active viewport editing session: null — no mode (the panel has no border);
    // the session document is the target of Undo from the viewport.
    std::function<void()> m_viewportEditingStop;
    Panda::AssetId m_viewportEditingDocument = 0;
    ShortcutManager m_shortcuts;
    int m_nextEntity = 1; // counter for the names of new entities
};

} // namespace Editor
