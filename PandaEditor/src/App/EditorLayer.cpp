#include "EditorLayer.hpp"
#include "UI/Settings/CoreSettingsSections.hpp"
#include "App/SystemStats.hpp"

#include "Project/Export/GameExporter.hpp"
#include "Project/Loader/AssetHandlerEditor.hpp"
#include "Project/Modules/ProjectModules.hpp"
#include "Project/System/PandaSdkLocator.hpp"
#include "Project/System/SystemColorSampler.hpp"
#include "Project/System/SystemTools.hpp"
#include "UI/EditorDialogs.hpp"
#include "Viewport/EditorWorldPasses.hpp"

#include <MirenTools.hpp>

#include <Panda/Assets/Base/AssetImporterBase.hpp>
#include <Panda/Renderer/MirenViewDistribution.hpp>
#include <Panda/Renderer/SceneWind.hpp>
#include <Panda/Renderer/ShaderInterface/ShaderInterface.hpp>
#include <Panda/GameLogic/GameContext.hpp>
#include <Panda/Serialization/SettingsMapper.hpp>
#include <Panda/Serialization/WorldMapper.hpp>
#include <Panda/WorldCommands/Impl/AddRemoveEntitiesCommand.hpp>
#include <Foundation/GraphicsAPI.hpp>
#include <Panda/Base/Version.hpp>
#include <Foundation/PlatformDetection.hpp>
#include <Fern/Fonts/FontCatalog.hpp>
#include <Fern/Fern.hpp>
#include <Fern/Events/WindowEvents.hpp>
#include <Fern/Events/Key.hpp>
#include <Fern/Events/KeyEvents.hpp>
#include <PandaUI/PandaUI.hpp>

#include "UI/EditorController.hpp" // shared coordinator of the PandaUI editor (migration)
#include <PandaEditorKit/EditorExtensions.hpp>
#include <PandaEditorKit/EditorHost.hpp>
#include <Rain/Coders/JsonDecoder.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace Panda {

namespace {

    struct ExportProjectOpenCommand {
        const char *generatedProjectExtension;
        const char *projectTypeName;
    };

    ExportProjectOpenCommand exportProjectOpenCommand(ExportProjectTarget target) {
        switch (target) {
            case ExportProjectTarget::XcodeMacOS:
            case ExportProjectTarget::XcodeIOSSimulator:
            case ExportProjectTarget::XcodeIOSDevice: return {".xcodeproj", "Xcode"};
            case ExportProjectTarget::VisualStudioWindows: return {".sln", "Visual Studio"};
        }
        PND_ASSERT(false, "Unknown export project target");
        return {"", ""};
    }

    std::optional<path_t> findGeneratedProjectFile(const path_t &directory, const char *extension) {
        if (!std::filesystem::exists(directory)) { return std::nullopt; }

        for (const auto &entry : std::filesystem::directory_iterator(directory)) {
            if (entry.path().extension() == extension) { return entry.path(); }
        }
        return std::nullopt;
    }

    void logCommandOutput(std::string_view line) {
        if (!line.empty()) { LOG_INFO_EDITOR("%.*s", static_cast<int>(line.size()), line.data()); }
    }

} // namespace

EditorLayer::EditorLayer(Fern::Window *window)
    : m_editingWorld()
    , m_playingWorld()
    , m_currentWorld(&m_editingWorld)
    , m_sceneState(SceneState::EDIT)
    , m_loader(&m_editingWorld, this)
    , m_pendingWorldLoad(0)
    , m_editorCamera()
    , m_cameraController()
    , m_editorHelpers()
    , m_grid(&m_cameraController)
    , m_window(window)
    , m_menuBar(window, this)
    , m_pandaUI(window)
    , m_viewport(&m_cameraController) {}

EditorLayer::~EditorLayer() = default; // Editor::EditorController is complete here (see include) — for unique_ptr

void EditorLayer::onAttach() {
    Foundation::EditorLogger::init(Editor::OutputPanelView::loggerCallback); // engine logs → Output panel
    // The user shader compiler is slangc from the editor bundle (or PandaSDK): without it
    // MirenTools would fall back to "slangc" from PATH, which the user does not have.
    if (std::optional<path_t> slangc = SystemTools::findSlangExecutable()) {
        MirenTools::setSlangcExecutable(*slangc);
    } else {
        LOG_ERROR_EDITOR("slangc not found (bundle/SDK): user shader compilation will fail");
    }
    // The generated UBO blocks (default-shaders/include/panda) must match the C++ structs of
    // ShaderBlocks.hpp: a mismatch means the layout has drifted and the files were not regenerated.
    const std::string staleIncludes = ShaderInterface::checkIncludes(AssetImporterBase::getShaderIncludeRoot());
    if (!staleIncludes.empty()) { LOG_ERROR_EDITOR("%s", staleIncludes.c_str()); }
    GameContext::setCurrentWorld(m_currentWorld);
    // A script quit in the editor = Stop of the game, not closing the application. Deferred: the
    // request arrives in the middle of the world update — stop() tears down the playing world and
    // would crash the script frame in progress.
    GameContext::setQuitRequestHandler([this] { m_pendingStop = true; });
    Fern::Size dpi = Application::get()->getMainWindow()->getDpi();
    Fern::Size windowSize = Application::get()->getMainWindow()->getSize();
    // Default framebuffer initial size
    Miren::setViewport(
        Views::GLOBAL_VIEW, Miren::Rect(0, 0, windowSize.width * dpi.width, windowSize.height * dpi.height)
    );
    m_viewport.initWithSize(Size(100.f, 100.f));
    m_viewport.setCamera(&m_editorCamera);
    m_viewport.setWorld(m_currentWorld);
    m_cameraController.setPosition({0.f, 0.f, 4.f});
    m_loader.loadInitialData();
    // The wind of the loaded world goes into SceneWind BEFORE the UI is built: pinned material
    // tabs build their forms in applyDockLayout and must show the world value of the system
    // fields (windStrength/windGustiness), not the default.
    SceneWind::apply(m_editingWorld.getWindSettings());
    if (!m_loader.hasOpenedProject()) { updateWindowState(); }
    m_menuBar.rebuild();

    // PandaUI host of the editor chrome (renders on GLOBAL_VIEW). Fonts and icons go BEFORE the
    // controller is created (panels take icons from the global registry while being built). The
    // coordinator is Editor::EditorController.
    installSystemColorSampler(); // ColorPicker eyedropper (macOS: NSColorSampler; otherwise no-op)
    m_editorUI.initialize(m_window, Views::GLOBAL_VIEW);
    m_editorUI.loadFonts(fontSettings());
    // The list of installed fonts for the APPEARANCE section is scanned once, off the main thread.
    Application::get()->getScheduler().submit(
        PandaAsync::JobDesc{"fonts.scan", PandaAsync::Priority::Background}, [](PandaAsync::Context &) { Fern::Fonts::families(); }
    );
    m_editorUI.loadEditorIcons();
    loadPlugins();
    // Before the controller: panels read the theme while being built.
    applyTheme(m_loader.editorSettings().get<EditorAppearanceSettings>().theme);
    // Asset previews/thumbnails: a single manager, panels go through ThumbnailsManager::shared().
    m_thumbnails = std::make_unique<ThumbnailsManager>([this](Miren::TextureHandle native, PandaUI::Size size) {
        return m_editorUI.wrapNativeTexture(native, size);
    });
    ThumbnailsManager::setShared(m_thumbnails.get());
    m_startScreenController =
        std::make_unique<Editor::StartScreenController>(m_editorUI.loadImage("ui/start/Logo.png"), this);
    m_startScreenController->setNewProjectDefaults("UntitledGame", defaultProjectParentPath().string());
    syncStartScreenModel();
    m_editorController = std::make_unique<Editor::EditorController>();
    m_editorController->setWorld(&m_editingWorld); // panels read the real engine world directly
    m_editorController->setWorldDisplayName(worldDisplayName()); // the world could load before the controller
    // A ready thumbnail → targeted update of the visible grid cell (cells are configured once).
    m_thumbnails->setOnThumbnailReady([this](Panda::AssetId assetId) {
        if (m_editorController) { m_editorController->thumbnailDidBecomeReady(assetId); }
    });
    m_editorUI.context().setUnhandledKeyDownHandler(
        [this](const PandaUI::KeyEvent &event) {
            if (!m_loader.hasOpenedProject() || !m_editorController) { return false; }
            return m_editorController->handleShortcut(event);
        }
    );
    m_editorController->setBridge(this); // save / play-simulate-stop / reload / gizmo / rect-select — a single seam
    // The themes for the APPEARANCE section of the editor settings (the data plugins live here).
    Editor::setThemeProvider({
        [this] { return themeNames(); },
        [this] { return currentThemeIndex(); },
        [this](int index) { selectThemeIndex(index); },
    });
    // The updater for the UPDATES section of the editor settings.
    Editor::setUpdateProvider({
        EditorUpdater::isEnabled(),
        [this] { checkForUpdates(true); },
        [this] { return updateStatusLine(); },
    });
    loadEditorParts(); // parts host: the coordinator itself (since its creation) // project open at startup: module parts before layout (tab forms wait for them)
    m_editorController->applyDockLayout(m_loader.getDockLayout()); // saved dock layout (if any)
    m_viewportSettings = m_loader.editorSettings().get<EditorViewportSettings>();
    m_cameraController.setMoveSpeed(m_viewportSettings.cameraSpeed);
    if (m_loader.hasOpenedProject()) {
        showEditorChrome();
    } else {
        showStartScreen();
    }
    // After the settings and the chrome: the first check runs at once when automatic updates are on.
    m_updater.start(m_loader.editorSettings().get<EditorUpdateSettings>().autoUpdate);
    m_pluginStore.start(m_loader.editorSettings().get<EditorUpdateSettings>().autoUpdate); // plugin updates follow the same switch

    LOG_INFO_EDITOR("EDITOR INITIALIZED");
}

void EditorLayer::onDetach() {
    Fern::setCursorLocked(false);
    ThumbnailsManager::setShared(nullptr);
    m_thumbnails.reset();
    m_editorUI.shutdown();
    m_pandaUI.shutdown();
    GameContext::setQuitRequestHandler(nullptr);
    GameContext::setWorldLoadRequestHandler(nullptr);
    GameContext::setCurrentWorld(nullptr);
}

void EditorLayer::onUpdate(double deltaTime) {
    m_stripFrameCounter++;
    updateStripStatus(deltaTime);
    m_updater.tick(deltaTime);
    pushUpdateStatus();
    // Wind of the ACTIVE world (editing/playing) — every frame before the worlds and panels are
    // updated: Settings edits and world switches reach the system fields of materials.
    if (m_currentWorld) { SceneWind::apply(m_currentWorld->getWindSettings()); }
    if (m_pendingStop) { // script quit from the previous frame → Stop before the worlds update begins
        m_pendingStop = false;
        if (m_sceneState != SceneState::EDIT) { stop(); }
    }
    // Size the engine viewport from the PandaUI panel BEFORE the scene renders this frame,
    // and gate camera navigation on the PandaUI viewport's focus/hover.
    if (m_loader.hasOpenedProject()) {
        syncPandaUIEditorViewport();
        updatePandaUICameraActivity();
        m_viewport.pollSelection(); // apply async rect-select / click-picking results
        m_editorController->syncSelectionFromEngine(); // SelectionContext → panels (viewport → Hierarchy/Inspector)
        m_editorController->syncWorldStructure(); // scripts created/deleted entities → live Hierarchy
        m_editorController->updateChromeFromState(m_sceneState == SceneState::EDIT); // toolbar + dirty scene tab
        m_editorController->setViewportGameInput(m_sceneState == SceneState::PLAY); // viewport clicks go to the game
        pumpProjectBuild(); // cmake output → CMake panel; on a successful scripts Build — reload the dylib
    }

    if (m_loader.hasOpenedProject()) {
        // Every frame, applied to the CURRENT world: the playing world is created on Play with a fresh renderer.
        m_currentWorld->getRenderer3D().setForceWireframe(m_viewportSettings.wireframe);
        switch (m_sceneState) {
            case SceneState::EDIT: {
                m_cameraController.update(deltaTime);
                glm::mat4 view = m_cameraController.getViewMatrix();
                glm::mat4 proj = m_editorCamera.getProjection();
                glm::mat4 cameraViewProj = proj * m_cameraController.getSkyViewMatrix();
                glm::mat4 viewProj = proj * view;
                updateViewportTools(deltaTime); // brushes — before the world update: edits are remeshed this frame
                const WorldFrame frame{
                    .viewProj = viewProj, .skyViewProj = cameraViewProj, .cameraPosition = m_cameraController.getPosition()
                };
                m_editingWorld.update(deltaTime, frame);
                EditorWorldPasses::renderSelectionOverlay(m_editingWorld, viewProj);
                EditorWorldPasses::renderPicking(m_editingWorld, frame);
                m_grid.update(viewProj);
                if (m_viewportSettings.showHelperIcons) {
                    m_editorHelpers.render(&m_editingWorld, viewProj, m_editorCamera.getViewportSize());
                }
                break;
            }
            case SceneState::PLAY: {
                m_viewport.setCamera(m_playingWorld.findMainCamera());
                if (m_playingWorld.isRunning()) {
                    m_playingWorld.update(deltaTime, WorldFrame{.mainCamera = true, .simulate = true});
                }
                syncPandaUIViewportFrame();
                m_pandaUI.update(static_cast<float>(deltaTime));
                loadPendingWorldRequest();
                break;
            }
            case SceneState::SIMULATE: {
                m_cameraController.update(deltaTime);
                glm::mat4 view = m_cameraController.getViewMatrix();
                glm::mat4 proj = m_editorCamera.getProjection();
                glm::mat4 cameraViewProj = proj * m_cameraController.getSkyViewMatrix();
                glm::mat4 viewProj = proj * view;
                m_playingWorld.update(
                    deltaTime, WorldFrame{
                                   .viewProj = viewProj,
                                   .skyViewProj = cameraViewProj,
                                   .cameraPosition = m_cameraController.getPosition(),
                                   .simulate = true,
                               }
                );
                EditorWorldPasses::renderSelectionOverlay(m_playingWorld, viewProj);
                syncPandaUIViewportFrame();
                m_pandaUI.update(static_cast<float>(deltaTime));
                loadPendingWorldRequest();
                m_grid.update(viewProj);
                break;
            }
        }
        updateViewportHelperLines(); // after the camera update — collider outlines without a one-frame lag
        m_thumbnails->update();      // live material preview + one thumbnail from the queue
        updatePostprocessing();      // main camera chain: outline reads its result
        m_viewport.drawOutline(deltaTime);
        GameContext::getAssetHandler()->cleanup();
    }

    if (m_loader.hasOpenedProject()) {
        // Live statistics: the current world has already rendered the frame (EDIT/PLAY/SIMULATE) —
        // the stats are current.
        // Layout passes of the chrome UI accumulate over a one-second window: the panel itself dirties
        // the layout by changing text (background ~2-3/s), so a per-frame value would flicker; a
        // pathology shows up as ~FPS passes per second.
        const Renderer2D::Statistics stats2d = m_currentWorld->getRenderer2D().getStats();
        const Renderer3D::Statistics stats3d = m_currentWorld->getRenderer3D().getStats();
        const uint64_t uiLayoutCount = m_editorUI.context().layoutPassCount();
        m_uiLayoutAccum += static_cast<uint32_t>(uiLayoutCount - m_lastUiLayoutCount);
        m_lastUiLayoutCount = uiLayoutCount;
        m_uiLayoutWindow += static_cast<float>(deltaTime);
        if (m_uiLayoutWindow >= 1.f) {
            m_uiLayoutsPerSecond = m_uiLayoutAccum;
            m_uiLayoutAccum = 0;
            m_uiLayoutWindow -= 1.f;
        }
        m_editorController->setFrameStatistics(
            Application::get()->fps, stats3d.drawCalls, stats3d.shadowDrawCalls, stats2d.drawCalls,
            stats2d.quadCount, m_uiLayoutsPerSecond
        );
        m_editorController->collectExtensionStatistics(); // contribution sections (flora, etc.)
        m_profilerRefreshWindow += static_cast<float>(deltaTime);
        if (m_profilerRefreshWindow >= 0.25f) {
            m_profilerRefreshWindow = 0.f;
            m_editorController->updateProfilerPanel();
        }
    }

    if (m_loader.hasOpenedProject()) {
        if (Editor::editorExtensions().viewportTools.anyDriving()) {
            // The active viewport tool takes the LMB: the gizmo does not manipulate and hides.
            m_editorController->setGizmoVisible(false);
            m_editorController->setGizmoActive(false);
        } else {
            drivePandaUIGizmo(); // gizmo: input+manipulation BEFORE the overlay renders
        }
    }
    m_editorUI.update(deltaTime); // PandaUI chrome on top of the scene
}

void EditorLayer::syncPandaUIEditorViewport() {
    if (!m_editorController) { return; }
    const PandaUI::Rect frame = m_editorController->viewportSceneFrame();
    if (frame.size.width < 1.f || frame.size.height < 1.f) { return; } // PandaUI layout not computed yet
    // Drive the engine viewport (FBO size + camera aspect + origin) from the PandaUI panel's on-screen frame.
    m_viewport.syncExternalFrame(
        Rect(frame.origin.x, frame.origin.y, frame.size.width, frame.size.height)
    );
    // Wrap the scene's result texture once (the native handle is stable across resizes) and feed the panel.
    Miren::TextureHandle native = m_viewport.getResultTexture();
    if (native.isValid() && native.id != m_sceneNative.id) {
        m_sceneTexture = m_editorUI.wrapNativeTexture(native, PandaUI::Size(frame.size.width, frame.size.height));
        m_sceneNative = native;
    }
    if (m_sceneTexture.isValid()) {
        // GL renders FBO textures bottom-left origin (needs flip), D3D — top-left.
        m_editorController->setViewportSceneTexture(m_sceneTexture, Foundation::ACTIVE_GRAPHICS_TRAITS.originBottomLeft);
    }
}

void EditorLayer::updatePandaUICameraActivity() {
    if (!m_editorController) { return; }
    const bool focused = m_editorController->isViewportFocused();
    const bool hovered = m_editorController->isViewportHovered();
    // WASD while hovering the viewport (like scroll-rotation) — BUT not while typing into a text field:
    // TextField takes characters via char events, while the raw KeyDown still falls through to Input, so
    // the camera movement is explicitly suppressed while any text input is focused.
    PandaUI::View *focusedView = m_editorUI.context().getFocusedView();
    const bool textInputFocused = focusedView && focusedView->isTextInput();
    m_cameraController.setRotationActive((Input::isTrackpadScroll() && hovered) || focused);
    m_cameraController.setMoveActive((focused || hovered) && !textInputFocused);
}

// EngineBridge — requests from the editor chrome (EditorController) to the engine/host.
void EditorLayer::save() {
    saveWorld();
}

void EditorLayer::openWorldAsset(uint32_t worldAssetId) {
    auto *assetHandler = static_cast<AssetHandlerEditor *>(GameContext::getAssetHandler());
    if (!assetHandler || !assetHandler->hasAsset(worldAssetId)) {
        LOG_ERROR_EDITOR("World asset %u not found", worldAssetId);
        return;
    }
    AssetInfo assetInfo = assetHandler->getInfo(worldAssetId);
    if (assetInfo.type != AssetType::WORLD) {
        LOG_ERROR_EDITOR("Asset %u is not a world", worldAssetId);
        return;
    }
    const path_t assetRelativePath = assetHandler->assetPath(worldAssetId);
    if (assetRelativePath.empty()) {
        LOG_ERROR_EDITOR("World asset %u has empty path", worldAssetId);
        return;
    }
    path_t worldPath = assetHandler->getProjectPath() / assetRelativePath;
    if (worldPath == m_loader.getOpenedWorldPath()) { return; } // already open
    runWithSaveCurrentWorldDialog([this, worldPath]() {
        if (!m_loader.openWorld(worldPath)) {
            LOG_ERROR_EDITOR("Failed to open world: %s", worldPath.string().c_str());
        }
    });
}

void EditorLayer::setSceneState(Editor::SceneState state) {
    if (state == Editor::SceneState::Play) {
        play();
    } else if (state == Editor::SceneState::Simulate) {
        simulate();
    } else {
        stop();
    }
}

void EditorLayer::reloadFromDisk() {
    // A single synchronization of the engine with the disk (Reload button / Cmd+R): assets →
    // scripts → world, everything by mtime — whatever has not changed on disk is not touched.
    // Conflict (the file is newer AND there are unsaved edits in memory) — a question for each
    // asset, Cancel aborts the rest of the operation. context/architecture.md.
    if (m_sceneState != SceneState::EDIT) { stop(); }
    auto *assetHandler = static_cast<AssetHandlerEditor *>(GameContext::getAssetHandler());
    if (!assetHandler) { return; }
    unsigned reloadedAssets = 0;
    for (AssetId id : assetHandler->collectAssetsChangedOnDisk()) {
        if (assetHandler->documents().isAssetChanged(id)) {
            const std::string title = "\"" + assetHandler->getAssetName(id) +
                                      "\" was changed on disk and has unsaved changes";
            const SystemTools::SaveChangesChoice choice = SystemTools::showSaveChangesDialog(
                title.c_str(), "Save yours to keep them, or discard to take the disk version?"
            );
            if (choice == SystemTools::SaveChangesChoice::Cancel) { return; }
            if (choice == SystemTools::SaveChangesChoice::Save) {
                // The editor's own edits go to disk — this asset needs no reload.
                assetHandler->documents().saveAsset(id);
                continue;
            }
        }
        assetHandler->reload(id); // the same path as "Don't Save" in tabs
        reloadedAssets++;
    }
    const bool scriptsReloaded = m_loader.isScriptsChangedOnDisk();
    if (scriptsReloaded) { m_loader.reloadScriptsDll(); }
    const bool worldReloaded = m_loader.isWorldChangedOnDisk();
    if (worldReloaded) { reloadOpenedWorld(); }
    if (reloadedAssets == 0 && !scriptsReloaded && !worldReloaded) {
        LOG_INFO_EDITOR("Reload: everything is up to date");
    } else {
        LOG_INFO_EDITOR(
            "Reload: %u assets%s%s", reloadedAssets, scriptsReloaded ? ", scripts" : "",
            worldReloaded ? ", world" : ""
        );
    }
}

void EditorLayer::setGizmoOperation(int mode) {
    const Gizmos::Operation op = mode == 1   ? Gizmos::Operation::Rotate
                                 : mode == 2 ? Gizmos::Operation::Scale
                                             : Gizmos::Operation::Translate;
    m_viewport.gizmo().setOperation(op);
}

void EditorLayer::setGizmoSpace(bool worldSpace) {
    m_viewport.gizmo().setSpace(worldSpace ? Gizmos::Space::World : Gizmos::Space::Local);
}

void EditorLayer::resetCamera() {
    m_cameraController.reset({0.f, 0.f, 0.f});
}

void EditorLayer::focusSelection() {
    if (!SelectionContext::isEmpty()) { m_cameraController.reset(SelectionContext::getMedianPosition()); }
}

void EditorLayer::startScriptsBuild(BuildAction action, ScriptsBuildConfig config) {
    if (!m_loader.hasOpenedProject()) { return; }
    const path_t projectPath = m_loader.getOpenedProjectPath();
    const path_t scriptsDir = projectPath / "Scripts" / projectPath.filename();
    if (m_projectBuilder.startScripts(action, config, scriptsDir)) {
        m_editorController->cmakeBuildStarted(BuildTargetKind::Scripts);
    }
}

void EditorLayer::startExportBuild(BuildAction action, ExportProjectTarget target) {
    if (!m_loader.hasOpenedProject()) { return; }
    if (m_projectBuilder.isRunning()) { return; }
    // The Export project consumes cooked data — it is prepared before cmake (as in the Export menu;
    // see cookRuntimeData).
    if (!cookRuntimeData()) {
        m_editorController->appendCMakeOutput(
            BuildTargetKind::Export, {"error: runtime data cook failed - see Output"}
        );
        return;
    }
    const path_t exportDir = m_loader.getOpenedProjectPath() / "Export";
    if (m_projectBuilder.startExport(action, target, exportDir)) {
        m_editorController->cmakeBuildStarted(BuildTargetKind::Export);
    }
}

void EditorLayer::cancelProjectBuild() {
    m_projectBuilder.cancel();
}

void EditorLayer::showProjectFolder(BuildTargetKind kind) {
    const path_t projectPath = m_loader.getOpenedProjectPath();
    if (projectPath.empty()) { return; } // no project open — nothing to show
    // The same path conventions as the build (ProjectBuilder): Scripts/<name>, Export/.
    const path_t folder = kind == BuildTargetKind::Export ? projectPath / "Export"
                                                          : projectPath / "Scripts" / projectPath.filename();
    if (!std::filesystem::exists(folder)) {
        LOG_ERROR_EDITOR("Project folder not found: %s", folder.string().c_str());
        return;
    }
    // openWithDefaultApplication: the folder is OPENED in Finder (SystemTools::open does a reveal
    // in the parent and does not escape spaces in the path).
    SystemTools::openWithDefaultApplication(folder);
}

TypeContext &EditorLayer::settingsStore(SettingsScope scope) {
    switch (scope) {
        case SettingsScope::Project: return m_loader.projectSettings();
        case SettingsScope::Editor: return m_loader.editorSettings();
        case SettingsScope::World: break;
    }
    return m_editingWorld.context(); // the world scope is the coordinator's (the panels' world)
}

void EditorLayer::editorSettingsChanged() {
    // The editor scope applies at once and is written on every change.
    m_viewportSettings = m_loader.editorSettings().get<EditorViewportSettings>();
    m_cameraController.setMoveSpeed(m_viewportSettings.cameraSpeed);
    m_updater.setAutoUpdate(m_loader.editorSettings().get<EditorUpdateSettings>().autoUpdate);
    m_pluginStore.setAutoUpdate(m_loader.editorSettings().get<EditorUpdateSettings>().autoUpdate);
    m_editorUI.applyFonts(fontSettings()); // a changed font or size: the atlases reload and the texts re-measure
    m_loader.saveEditorSettings();
}

bool EditorLayer::saveProjectSettings() {
    return m_loader.saveProjectSettings();
}

void EditorLayer::revertProjectSettings() {
    m_loader.reloadProjectSettings();
}

void EditorLayer::updateViewportTools(double deltaTime) {
    const glm::mat4 view = m_cameraController.getViewMatrix();
    const Editor::ViewportToolFrame frame{
        .world = m_editingWorld,
        .camera = m_editorCamera,
        .view = view,
        .viewProj = m_editorCamera.getProjection() * view,
        .cameraPosition = m_cameraController.getPosition(),
        .viewportHovered = m_editorController->isViewportHovered(),
        .deltaTime = deltaTime,
    };
    Editor::ViewportToolRegistry &tools = Editor::editorExtensions().viewportTools;
    for (const std::unique_ptr<Editor::ViewportTool> &tool : tools.tools()) { tool->update(frame); }
    m_editorController->setViewportToolCapture(tools.anyDriving());
}

void EditorLayer::loadEditorParts() {
    if (!m_loader.hasOpenedProject() || !m_editorController) { return; }
    const size_t iconFolders = Editor::editorExtensions().iconFolders.size();
    if (ProjectModules::loadEditorParts(m_loader.projectSettings().get<ProjectModuleSettings>()) > 0) {
        // The editor parts register their editor-scope sections after settings.json was read: the
        // sections kept as foreign values get their types now.
        SettingsMapper::adoptForeign(SettingsScope::Editor, m_loader.editorSettings());
    }
    const std::vector<path_t> &folders = Editor::editorExtensions().iconFolders;
    if (folders.size() > iconFolders) {
        std::vector<path_t> absolute; // module folders live in the bundle resources
        for (size_t i = iconFolders; i < folders.size(); i++) {
            absolute.push_back(Fern::getStaticResourcesPath() / folders[i]);
        }
        m_editorUI.loadIconFolders(absolute);
    }
    m_editorController->extensionsChanged();
}

void EditorLayer::updatePostprocessing() {
    // HDR: the scene is linear 16F, the pipeline ALWAYS runs (at minimum an implicit Tonemap).
    // The Settings toggle and camera.postprocessingEnabled control only the user chain.
    World *world = m_sceneState == SceneState::EDIT ? &m_editingWorld : &m_playingWorld;
    Entity cameraEntity = world->findMainCameraEntity();
    AssetRef<PostprocessingAsset> chain;
    PostprocessingFrameInputs inputs;
    inputs.color = m_viewport.getSceneColorTexture();
    inputs.emissive = m_viewport.getSceneEmissiveTexture();
    inputs.depth = m_viewport.getSceneDepthTexture();
    inputs.cameraNear = m_editorCamera.getNear(); // the viewport looks through the editor camera
    inputs.sunScreen = world->getFrameSunScreen(); // glare for bloom_apply
    inputs.cameraFar = m_editorCamera.getFar();
    if (cameraEntity.isValid()) {
        CameraComponent &camera = cameraEntity.getComponent<CameraComponent>();
        if (m_viewportSettings.showPostprocessing && camera.postprocessingEnabled) {
            chain = camera.postprocessing.as<PostprocessingAsset>();
        }
        inputs.bloom.enabled = m_viewportSettings.showPostprocessing && camera.bloomEnabled;
        inputs.bloom.intensity = camera.bloomIntensity;
        inputs.bloom.radius = camera.bloomRadius;
        inputs.exposure.enabled = m_viewportSettings.showPostprocessing && camera.autoExposureEnabled;
        inputs.exposure.key = camera.autoExposureKey;
        inputs.exposure.speed = camera.autoExposureSpeed;
        inputs.exposure.minExposure = camera.autoExposureMin;
        inputs.exposure.maxExposure = camera.autoExposureMax;
        if (m_sceneState != SceneState::EDIT) { // in PLAY/SIMULATE — the game camera
            inputs.cameraNear = camera.camera.getNear();
            inputs.cameraFar = camera.camera.getFar();
        }
    }
    if (!m_postprocessing) { m_postprocessing = std::make_unique<PostprocessingPipeline>(); }
    const Fern::Size dpi = Application::get()->getMainWindow()->getDpi();
    const Rect frame = m_viewport.getFrame();
    const Miren::TextureHandle output = m_postprocessing->apply(
        chain,
        inputs,
        (uint32_t)(frame.size.width * dpi.width),
        (uint32_t)(frame.size.height * dpi.height),
        Application::get()->getTime()
    );
    Miren::TextureHandle result; // invalid (zero-size frame) — outline reads the raw scene
    if (output.id != inputs.color.id) { result = output; }
    m_viewport.setPostprocessedColor(result);
}

void EditorLayer::updateViewportHelperLines() {
    // Helper lines are drawn with the editor camera over the world the viewport shows: EDIT — the
    // editing world, SIMULATE — the playing world (the runtime state lives there). In PLAY the
    // view is the game's — helpers are not shown.
    World *world = nullptr;
    if (m_sceneState == SceneState::EDIT) {
        world = &m_editingWorld;
    } else if (m_sceneState == SceneState::SIMULATE) {
        world = &m_playingWorld;
    }
    // The overlay draws as many lines as its share of the UI draw list holds, so they go in the
    // order of importance: the lines of the tools (the feedback of the edit in progress), then the
    // overlays of the modules (collider outlines from the nearest one).
    std::vector<HelperLine> lines;
    const glm::mat4 viewProj = m_editorCamera.getProjection() * m_cameraController.getViewMatrix();
    const PandaUI::Rect frame = m_editorController->viewportSceneFrame();
    if (m_sceneState == SceneState::EDIT) {
        for (const std::unique_ptr<Editor::ViewportTool> &tool : Editor::editorExtensions().viewportTools.tools()) {
            tool->appendHelperLines(lines, viewProj, frame);
        }
    }
    if (world) {
        for (const Editor::ViewportOverlay &overlay : Editor::editorExtensions().viewportOverlays) {
            if (overlay.appendHelperLines != nullptr) { overlay.appendHelperLines(*world, lines, viewProj, frame); }
        }
    }
    m_editorController->setViewportHelperLines(std::move(lines));
}

void EditorLayer::pumpProjectBuild() {
    std::vector<std::string> lines = m_projectBuilder.drainLines();
    if (!lines.empty()) { m_editorController->appendCMakeOutput(m_projectBuilder.activeKind(), lines); }
    if (std::optional<ProjectBuilder::Finished> finished = m_projectBuilder.takeFinished()) {
        m_editorController->cmakeBuildFinished(finished->kind, finished->success, finished->canceled);
        if (finished->success && finished->kind == BuildTargetKind::Scripts && finished->action == BuildAction::Build) {
            // Payoff of the author→build→run loop: the fresh dylib is picked up immediately, without
            // a manual reload.
            if (m_sceneState == SceneState::EDIT) {
                m_loader.reloadScriptsDll();
            } else {
                LOG_INFO_EDITOR("Scripts built. Stop the world to reload them.");
            }
        }
    }
}

void EditorLayer::rectSelect(PandaUI::Point start, PandaUI::Point current, bool additive, bool committed) {
    m_viewport.driveRectSelection(
        IVec2(static_cast<int>(start.x), static_cast<int>(start.y)),
        IVec2(static_cast<int>(current.x), static_cast<int>(current.y)),
        additive,
        committed
    );
}

void EditorLayer::drivePandaUIGizmo() {
    if (!m_editorController) { return; }
    // Gizmo input from Panda::Input (events not consumed by PandaUI leak into Input — see onEvent). The
    // position is in absolute screen points (like the getDrawData coordinates). blockedByUi=false: clicks
    // on the PandaUI toolbar are consumed by PandaUI itself and never reach Input.
    const Gizmo::InputState input{
        .mousePosition = Vec2(static_cast<float>(Input::getMousePositionX()), static_cast<float>(Input::getMousePositionY())),
        .mouseDown = Input::isMouseButtonPressed(Fern::MouseButton::LEFT),
        .mouseClicked = Input::isMouseButtonJustPressed(Fern::MouseButton::LEFT),
        .viewportHovered = m_editorController->isViewportHovered(),
        .blockedByUi = false,
    };
    // At runtime the physics moves the selected bodies by itself, so the selection median (the gizmo
    // center and the inspector's Transform fields) is refreshed once per frame; otherwise the gizmo
    // freezes at the body's starting position.
    if (m_currentWorld->isRunning() && !SelectionContext::isEmpty()) { SelectionContext::updateValues(); }
    // View of the active viewport camera: in PLAY the game camera (manipulation in its screen rays),
    // otherwise the editor camera.
    glm::mat4 view;
    const glm::mat4 *viewPtr = nullptr;
    if (m_sceneState == SceneState::PLAY) {
        Entity cameraEntity = m_currentWorld->findMainCameraEntity();
        if (cameraEntity.isValid()) {
            view = glm::inverse(m_currentWorld->getWorldSpaceTransformMatrix(cameraEntity));
            viewPtr = &view;
        }
    } else {
        view = m_cameraController.getViewMatrix();
        viewPtr = &view;
    }
    Gizmos &gizmo = m_viewport.gizmo();
    const bool visible = gizmo.manipulate(m_viewport.getFrame(), input, viewPtr);
    m_editorController->setGizmoVisible(visible);
    m_editorController->setGizmoActive(gizmo.isHovered()); // hover over a handle / drag → takes LMB from rect-select
}

void EditorLayer::onEvent(Fern::Event *event) {
    if (m_menuBar.handleEvent(event, m_editorUI.context().modals().isPresenting())) { return; }

    // In play/simulate the game's runtime UI (m_pandaUI) receives input first (as an overlay under the
    // editor chrome).
    if (m_sceneState != SceneState::EDIT) {
        syncPandaUIViewportFrame();
        if (event->type == Fern::EventType::WindowResize) { m_pandaUI.syncWindow(event->getWindow()); }
        if (m_pandaUI.handleEvent(event)) {
            event->isHandled = true;
            return;
        }
    }

    // All the remaining input goes to the PandaUI Context (editor chrome); quit/close/resize are
    // handled right here.
    bool handledByUI = m_editorUI.handleEvent(event); // Fern → PandaUI Context (WindowResize syncs the viewport)
    if (event->type == Fern::EventType::QuitRequest ||
        (event->type == Fern::EventType::WindowCloseRequest && event->getWindow() == m_window)) {
        closeAppRequest();
        event->isHandled = true;
    } else if (event->type == Fern::EventType::WindowResize && event->getWindow() == m_window) {
        const Fern::WindowResizeEvent *ev = static_cast<const Fern::WindowResizeEvent *>(event);
        windowSizeChanged(Size(ev->getWidth(), ev->getHeight()));
    } else if (handledByUI) {
        event->isHandled = true; // a PandaUI view consumed it (button/field/docking) — not passed to Input
    }
    // Otherwise do NOT mark it handled: Application feeds the event to Panda::Input (camera/picking
    // in the viewport).
}

void EditorLayer::windowSizeChanged(Size size) {
    Miren::Rect viewport =
        Miren::Rect(0, 0, size.width * m_window->getDpi().width, size.height * m_window->getDpi().height);
    Miren::setViewport(Views::GLOBAL_VIEW, viewport);
#ifdef PLATFORM_DESKTOP
    m_window->getDrawingContext()->update();
#endif
}

void EditorLayer::play() {
    m_sceneState = SceneState::PLAY;
    m_playingWorld = m_editingWorld;
    m_currentWorld = &m_playingWorld;
    GameContext::setCurrentWorld(m_currentWorld);
    GameContext::setWorldLoadRequestHandler([this](AssetId worldAssetId) { requestWorldLoad(worldAssetId); });
    m_viewport.setWorld(m_currentWorld);
    m_pendingWorldLoad = AssetId(0);
    syncPandaUIViewportFrame();
    m_pandaUI.initialize();
    if (m_editorController) { m_editorController->setActiveWorld(m_currentWorld); } // panels → the live world
    m_playingWorld.startRunning();
    loadPendingWorldRequest();
    m_viewport.focus();
}

void EditorLayer::simulate() {
    m_sceneState = SceneState::SIMULATE;
    m_playingWorld = m_editingWorld;
    m_currentWorld = &m_playingWorld;
    GameContext::setCurrentWorld(m_currentWorld);
    GameContext::setWorldLoadRequestHandler([this](AssetId worldAssetId) { requestWorldLoad(worldAssetId); });
    m_viewport.setWorld(m_currentWorld);
    m_pendingWorldLoad = AssetId(0);
    syncPandaUIViewportFrame();
    m_pandaUI.initialize();
    if (m_editorController) { m_editorController->setActiveWorld(m_currentWorld); } // panels → the live world
    m_playingWorld.startRunning();
    loadPendingWorldRequest();
    m_viewport.focus();
}

void EditorLayer::stop() {
    m_playingWorld.finishRunning();
    Fern::setCursorLocked(false);
    m_pandaUI.shutdown();
    m_pandaUI.clearMainViewportFrame();
    m_sceneState = SceneState::EDIT;
    m_pendingWorldLoad = AssetId(0);
    GameContext::setWorldLoadRequestHandler(nullptr);
    m_viewport.setCamera(&m_editorCamera);
    m_currentWorld = &m_editingWorld;
    GameContext::setCurrentWorld(m_currentWorld);
    m_viewport.setWorld(m_currentWorld);
    if (m_editorController) { m_editorController->setActiveWorld(m_currentWorld); } // panels → the editing world
    // Play is a simulation: its asset mutations (script ABI, e.g. terrain digging) do not
    // survive Stop — the affected assets are reloaded from disk. Unsaved EDITOR edits
    // in such an asset (only editor tools set dirty) are rolled back as well.
    if (auto *assetHandler = static_cast<AssetHandlerEditor *>(GameContext::getAssetHandler())) {
        for (AssetId id : GameContext::takeRuntimeMutatedAssets()) {
            if (!assetHandler->isLoaded(id)) { continue; }
            if (assetHandler->documents().isAssetChanged(id)) {
                LOG_WARN_EDITOR("STOP: unsaved editor changes of asset %u discarded (mutated in Play)", (uint32_t)id);
            }
            assetHandler->reload(id);
        }
    }
    m_viewport.focus();
}

void EditorLayer::requestWorldLoad(AssetId worldAssetId) {
    m_pendingWorldLoad = worldAssetId;
}

void EditorLayer::loadPendingWorldRequest() {
    if (!m_pendingWorldLoad || m_sceneState == SceneState::EDIT) { return; }

    AssetId worldAssetId = m_pendingWorldLoad;
    m_pendingWorldLoad = AssetId(0);
    if (!loadWorldAssetIntoPlayingWorld(worldAssetId)) {
        LOG_ERROR_EDITOR("Failed to load requested world %u", (uint32_t)worldAssetId);
    }
}

bool EditorLayer::loadWorldAssetIntoPlayingWorld(AssetId worldAssetId) {
    AssetHandlerEditor *assetHandler = static_cast<AssetHandlerEditor *>(GameContext::getAssetHandler());
    if (!assetHandler || !assetHandler->hasAsset(worldAssetId)) {
        LOG_ERROR_EDITOR("World asset %u not found", (uint32_t)worldAssetId);
        return false;
    }

    AssetInfo assetInfo = assetHandler->getInfo(worldAssetId);
    if (assetInfo.type != AssetType::WORLD) {
        LOG_ERROR_EDITOR("Asset %u is not a world", (uint32_t)worldAssetId);
        return false;
    }

    const path_t assetRelativePath = assetHandler->assetPath(worldAssetId);
    if (assetRelativePath.empty()) {
        LOG_ERROR_EDITOR("World asset %u has empty path", (uint32_t)worldAssetId);
        return false;
    }

    path_t worldPath = assetHandler->getProjectPath() / assetRelativePath;
    std::ifstream file(worldPath);
    if (!file.is_open()) {
        LOG_ERROR_EDITOR("World file not found: %s", worldPath.string().c_str());
        return false;
    }

    bool wasRunning = m_playingWorld.isRunning();
    if (wasRunning) { m_playingWorld.finishRunning(); }
    Rain::JsonDecoder decoder;
    const bool loaded = WorldMapper::load(m_playingWorld, decoder, file);
    file.close();
    if (!loaded) {
        LOG_ERROR_EDITOR("Failed to decode world: %s", worldPath.string().c_str());
        if (wasRunning) { m_playingWorld.startRunning(); }
        return false;
    }
    m_playingWorld.updateScriptsAndFields();
    m_viewport.setWorld(&m_playingWorld);
    if (m_sceneState == SceneState::PLAY) { m_viewport.setCamera(m_playingWorld.findMainCamera()); }
    if (wasRunning) { m_playingWorld.startRunning(); }
    return true;
}

void EditorLayer::showStartScreen() {
    if (!m_startScreenController) { return; }
    syncStartScreenModel();
    if (m_uiRootMode == UIRootMode::StartScreen) { return; }
    m_editorUI.setRootView(m_startScreenController->rootView());
    m_uiRootMode = UIRootMode::StartScreen;
}

void EditorLayer::showEditorChrome() {
    if (!m_editorController) { return; }
    if (m_uiRootMode == UIRootMode::Editor) { return; }
    m_editorUI.setRootView(m_editorController->rootView());
    m_uiRootMode = UIRootMode::Editor;
}

void EditorLayer::syncStartScreenModel() {
    if (!m_startScreenController) { return; }

    // A 1:1 mirror of the recent list — the start screen indices match the loader indices.
    std::vector<Editor::StartScreenRecentProject> projects;
    const std::vector<RecentProject> &recentProjects = m_loader.getRecentProjectsList();
    projects.reserve(recentProjects.size());
    for (const RecentProject &project : recentProjects) {
        std::string name = project.name.empty() ? path_t(project.path).filename().string() : project.name;
        if (name.empty()) { name = project.path; }
        projects.push_back({std::move(name), project.path});
    }
    m_startScreenController->setRecentProjects(std::move(projects));
}

path_t EditorLayer::defaultProjectParentPath() const {
    if (const char *home = std::getenv("HOME")) { return path_t(home); }
    return std::filesystem::current_path();
}

void EditorLayer::syncPandaUIViewportFrame() {
    Fern::Size dpi = m_window->getDpi();
    m_pandaUI.setMainViewportFrame(m_viewport.getFrame(), Size(dpi.width, dpi.height));
}

#pragma region Project loader output

void EditorLayer::loaderDidLoadProject(const std::string &name, const path_t &path) {
    // Game saves, when launched from the editor, live next to the project.
    const path_t saveDataPath = path / ".Panda" / "SaveData";
    std::filesystem::create_directories(saveDataPath);
    GameContext::setPersistentDataPath(saveDataPath);
    updateWindowState();
    m_menuBar.rebuild();
    // The thumbnail cache belongs to the previous project: asset ids may coincide between projects.
    if (m_thumbnails) { m_thumbnails->clear(); }
    // Asset panels rescan the new project (at startup the controller does not exist yet —
    // then setWorld reads the registry right after creation).
    if (m_editorController) { m_editorController->refreshAssetPanels(); }
    loadEditorParts(); // at startup the controller does not exist yet — then after its creation (onAttach)
    showEditorChrome();
}

std::string EditorLayer::worldDisplayName() {
    const path_t &worldPath = m_loader.getOpenedWorldPath();
    return worldPath.empty() ? "Untitled World" : worldPath.filename().stem().string();
}

void EditorLayer::loaderDidLoadWorld() {
    SelectionContext::unselectAll();
    if (m_editorController) { m_editorController->worldContextChanged(); }
    const path_t &worldPath = m_loader.getOpenedWorldPath();
    if (worldPath.empty()) {
        m_window->setTitle("Untitled World");
    } else {
        std::string title = worldPath.filename().string();
        m_window->setTitle(title.c_str());
    }
    m_menuBar.rebuild();
    if (m_editorController) { m_editorController->setWorldDisplayName(worldDisplayName()); }
    if (m_editorController) { m_editorController->refreshFromWorld(); } // migration: re-read the hierarchy tree
}

void EditorLayer::loaderDidLoadCloseProject() {
    m_editingWorld.clear();
    m_playingWorld.clear();
    SelectionContext::unselectAll();
    if (m_editorController) { m_editorController->worldContextChanged(); }
    m_sceneTexture = {};
    m_sceneNative = {};
    if (m_editorController) { m_editorController->setViewportSceneTexture({}, false); }
    updateWindowState();
    m_menuBar.rebuild();
    if (m_editorController) { m_editorController->refreshFromWorld(); }
    if (m_startScreenController) {
        m_startScreenController->showRecentProjects();
        m_startScreenController->setStatus("Select a project or create a new one.");
    }
    showStartScreen();
}

void EditorLayer::loaderDidCreateNewWorld() {
    SelectionContext::unselectAll();
    if (m_editorController) { m_editorController->worldContextChanged(); }
    m_window->setTitle("Untitled World");
    m_playingWorld.clear();
    m_menuBar.rebuild();
    if (m_editorController) { m_editorController->refreshFromWorld(); }
}

EditorCameraSettings EditorLayer::getEditorCameraSettings() {
    EditorCameraSettings settings;
    settings.position = m_cameraController.getPosition();
    settings.rotation = m_cameraController.getRotation();
    return settings;
}

void EditorLayer::setEditorCameraSettings(EditorCameraSettings settings) {
    m_cameraController.setPosition(settings.position);
    m_cameraController.setRotation(settings.rotation);
}

#pragma endregion

#pragma region Menu bar output

void EditorLayer::menuBarUndo() {
    if (m_editorController) { m_editorController->undo(); } // undo — coordinator domain (content edit, editing world)
}

void EditorLayer::menuBarRedo() {
    if (m_editorController) { m_editorController->redo(); }
}

bool EditorLayer::menuBarHasOpenedProject() {
    return m_loader.hasOpenedProject();
}

void EditorLayer::menuBarOpenWorldSettings() {
    if (m_editorController) { m_editorController->openSettings(SettingsScope::World); }
}

void EditorLayer::menuBarOpenProjectSettings() {
    if (m_editorController) { m_editorController->openSettings(SettingsScope::Project); }
}

void EditorLayer::menuBarOpenEditorSettings() {
    if (m_editorController) { m_editorController->openSettings(SettingsScope::Editor); }
}

// "Show … File": the JSON of the scope selected in the file manager (the editor has no text
// editor). A file that was never written (a fresh install, a project made outside the editor)
// is written first with the values the editor has, so there is something to show.
void EditorLayer::menuBarShowEditorSettingsFile() {
    const path_t path = m_loader.editorSettingsPath();
    if (path.empty()) {
        LOG_ERROR_EDITOR("No user data directory: the editor settings are not persisted");
        return;
    }
    if (!std::filesystem::exists(path)) { m_loader.saveEditorSettings(); }
    SystemTools::show(path);
}

void EditorLayer::menuBarShowProjectSettingsFile() {
    const path_t path = m_loader.projectSettingsPath();
    if (path.empty()) { return; } // no project open
    if (!std::filesystem::exists(path) && !m_loader.saveProjectSettings()) { return; }
    SystemTools::show(path);
}

void EditorLayer::menuBarOpenProject() {
    runWithSaveCurrentWorldDialog([this]() { openProjectFolderDialog(); });
}

void EditorLayer::menuBarOpenProject(const RecentProject &project) {
    path_t projectPath = project.path;
    if (projectPath == m_loader.getOpenedProjectPath()) { return; }
    runWithSaveCurrentWorldDialog([this, projectPath]() {
        saveOpenedProjectWindowState();
        m_loader.openProject(projectPath);
    });
}

void EditorLayer::menuBarNewWorld() {
    runWithSaveCurrentWorldDialog([this]() { m_loader.newWorld(); });
}

const std::vector<RecentProject> &EditorLayer::menuBarGetRecentProjectsList() {
    return m_loader.getRecentProjectsList();
}

const path_t &EditorLayer::menuBarGetOpenedProjectPath() {
    return m_loader.getOpenedProjectPath();
}

void EditorLayer::menuBarCloseApp() {
    closeAppRequest();
}

void EditorLayer::menuBarShowCppProject() {
    m_loader.showCppProject();
}

void EditorLayer::menuBarSaveWorld() {
    saveWorld();
}

void EditorLayer::menuBarReload() {
    reloadFromDisk();
}

void EditorLayer::reloadOpenedWorld() {
    // Re-reads the CURRENT world from disk: closes the loop of external .world edits (agent,
    // code editor, git) without a restart. The unsaved-changes dialog is the same as when
    // opening another world; a broken file only logs an error — loadWorld does not touch the
    // current world in that case.
    const path_t worldPath = m_loader.getOpenedWorldPath();
    if (worldPath.empty()) {
        LOG_WARN_EDITOR("Reload skipped: current world has no file yet");
        return;
    }
    runWithSaveCurrentWorldDialog([this, worldPath]() {
        if (!m_loader.openWorld(worldPath)) {
            LOG_ERROR_EDITOR("Failed to reload world: %s", worldPath.string().c_str());
        }
    });
}

void EditorLayer::reloadWorldAsset(uint32_t worldAssetId) {
    // Reimport in Content Browser: only the OPEN world is re-read; another world is neither
    // touched nor opened (there is nowhere to load it — the world lives in ProjectLoader).
    auto *assetHandler = static_cast<AssetHandlerEditor *>(GameContext::getAssetHandler());
    if (!assetHandler || !assetHandler->hasAsset(worldAssetId)) { return; }
    const path_t assetRelativePath = assetHandler->assetPath(worldAssetId);
    if (assetRelativePath.empty()) { return; }
    if (assetHandler->getProjectPath() / assetRelativePath != m_loader.getOpenedWorldPath()) {
        return;
    }
    reloadOpenedWorld();
}

void EditorLayer::menuBarCloseProject() {
    runWithSaveCurrentWorldDialog([this]() { closeProject(); });
}

void EditorLayer::loadPlugins() {
    // Built-in plugins come from the editor resources, installed ones live next to the user data
    // (the store uses Plugins/ and registry/ inside it). Icons become textures through the UI host.
    m_pluginStore.initialize(
        {Fern::getStaticResourcesPath() / "plugins"}, SystemTools::userDataDirectory(),
        [this](const path_t &file) { return m_editorUI.loadImage(file); }
    );
    // An install, an update or a removal takes effect at once: the theme of the settings is read
    // again (its files may have changed), or the editor falls back to the built-in dark one; the
    // icons of the plugins replace or release the editor's.
    m_pluginStore.setOnInstalledChanged([this] {
        applyTheme(m_loader.editorSettings().get<EditorAppearanceSettings>().theme);
        applyIconPlugins();
    });
    applyIconPlugins();
}

void EditorLayer::applyIconPlugins() {
    std::vector<path_t> files; // install order: built-in plugins first, a later plugin wins a token
    for (const InstalledPlugin &plugin : m_pluginStore.installed()) {
        files.insert(files.end(), plugin.icons.begin(), plugin.icons.end());
    }
    m_editorUI.applyIconOverrides(files);
}

EditorUI::FontSettings EditorLayer::fontSettings() {
    const EditorAppearanceSettings &appearance = m_loader.editorSettings().get<EditorAppearanceSettings>();
    EditorUI::FontSettings settings;
    settings.uiFamily = appearance.uiFont;
    settings.monoFamily = appearance.monoFont;
    settings.uiSize = appearance.uiFontSize;
    return settings;
}

void EditorLayer::applyTheme(const std::string &themeId) {
    if (const EditorTheme *theme = m_pluginStore.findTheme(themeId)) {
        if (std::optional<PandaUI::Theme> loaded = EditorPlugins::loadTheme(theme->file)) {
            PandaUI::setTheme(*loaded); // Context re-reads the colors of the whole tree
            return;
        }
        LOG_ERROR_EDITOR("Theme %s can't be read: %s", themeId.c_str(), theme->file.string().c_str());
    }
    PandaUI::setTheme(PandaUI::Theme::dark());
}

std::vector<std::string> EditorLayer::themeNames() {
    std::vector<std::string> names;
    for (const EditorTheme &theme : m_pluginStore.themes()) { names.push_back(theme.name); }
    return names;
}

int EditorLayer::currentThemeIndex() {
    const std::vector<EditorTheme> &themes = m_pluginStore.themes();
    const std::string &current = m_loader.editorSettings().get<EditorAppearanceSettings>().theme;
    for (size_t i = 0; i < themes.size(); i++) {
        if (themes[i].id == current) { return static_cast<int>(i); }
    }
    return -1;
}

void EditorLayer::selectThemeIndex(int index) {
    const std::vector<EditorTheme> &themes = m_pluginStore.themes();
    if (index < 0 || index >= static_cast<int>(themes.size())) { return; }
    selectTheme(themes[static_cast<size_t>(index)].id);
}

Panda::PluginStore &EditorLayer::pluginStore() {
    return m_pluginStore;
}

std::string EditorLayer::currentThemeId() {
    return m_loader.editorSettings().get<EditorAppearanceSettings>().theme;
}

void EditorLayer::selectTheme(const std::string &themeId) {
    if (!m_pluginStore.findTheme(themeId)) { return; }
    m_loader.editorSettings().get<EditorAppearanceSettings>().theme = themeId;
    m_loader.saveEditorSettings();
    applyTheme(themeId);
}

void EditorLayer::installDevPlugin() {
    // The native folder dialog blocks: the store reports problems as a notice the tab shows.
    std::optional<path_t> folder = SystemTools::openFolderDialog();
    if (!folder.has_value()) { return; }
    m_pluginStore.installDev(*folder);
}

void EditorLayer::menuBarOpenPlugins() {
    if (m_editorController) { m_editorController->openPlugins(); }
}

void EditorLayer::menuBarAbout() {
    Editor::AboutInfo info;
    info.version = EditorUpdater::versionString();
    info.channel = ENGINE_RELEASE_CHANNEL;
    info.build = ENGINE_BUILD_NUMBER;
    info.commit = ENGINE_BUILD_COMMIT;
    info.platform = EditorUpdater::platformString();
    info.updatesEnabled = EditorUpdater::isEnabled();
    if (m_updater.state().kind != UpdateState::Kind::Idle) { info.updateStatus = updateStatusLine(); }
    info.onCheckForUpdates = [this] { checkForUpdates(true); };
    info.onReleaseNotes = [this] { SystemTools::openUrl(m_updater.releaseNotesUrl()); };
    Editor::showAboutDialog(&m_editorUI.context(), info);
}

void EditorLayer::menuBarCheckForUpdates() {
    checkForUpdates(true);
}

void EditorLayer::menuBarRestartToUpdate() {
    if (m_updater.hasReadyUpdate()) { restartEditor(); }
}

void EditorLayer::menuBarViewReleaseNotes() {
    SystemTools::openUrl(m_updater.releaseNotesUrl());
}

bool EditorLayer::menuBarUpdateReady() {
    return m_updater.hasReadyUpdate();
}

void EditorLayer::checkForUpdates(bool manual) {
    if (!EditorUpdater::isEnabled()) {
        if (manual) {
            Editor::showMessageDialog(
                &m_editorUI.context(), "Updates are off in a local build",
                "This editor was built from the sources, not by the release workflow, so it follows no release "
                "channel. Preview builds that update themselves come from pandaengine.dev/download."
            );
        }
        return;
    }
    m_updater.check(manual);
}

void EditorLayer::updateIndicatorAction() {
    const UpdateState &state = m_updater.state();
    switch (state.kind) {
        case UpdateState::Kind::Ready:
            Editor::showUpdateReadyDialog(
                &m_editorUI.context(), state.version, state.notes, [this] { restartEditor(); },
                [this] { SystemTools::openUrl(m_updater.releaseNotesUrl()); }
            );
            break;
        case UpdateState::Kind::Updated:
            SystemTools::openUrl(m_updater.releaseNotesUrl());
            m_updater.dismiss();
            break;
        case UpdateState::Kind::Error:
            Editor::showMessageDialog(&m_editorUI.context(), "Update failed", state.message);
            break;
        case UpdateState::Kind::UpToDate:
            m_updater.dismiss();
            break;
        default:
            break;
    }
}

void EditorLayer::updateIndicatorDismiss() {
    m_updater.dismiss();
}

void EditorLayer::startScreenDidActivateUpdate() {
    updateIndicatorAction();
}

void EditorLayer::startScreenDidDismissUpdate() {
    m_updater.dismiss();
}

Editor::UpdateIndicatorModel EditorLayer::updateIndicatorModel() const {
    using Kind = UpdateState::Kind;
    using Shown = Editor::UpdateIndicatorKind;
    Editor::UpdateIndicatorModel model;
    const UpdateState &state = m_updater.state();
    if (m_updater.isDismissed()) { return model; }
    switch (state.kind) {
        case Kind::Idle: break;
        case Kind::Checking:
            model.kind = Shown::Checking;
            model.text = "Checking for updates…";
            break;
        case Kind::Downloading:
            model.kind = Shown::Downloading;
            model.progress = state.progress;
            model.text = state.progress >= 0.f ? "Downloading update " + std::to_string(int(state.progress * 100.f)) + "%"
                                               : "Downloading update…";
            break;
        case Kind::Installing:
            model.kind = Shown::Installing;
            model.text = "Installing update…";
            break;
        case Kind::Ready:
            model.kind = Shown::Ready;
            model.text = "Update ready · Restart";
            model.clickable = true;
            break;
        case Kind::UpToDate:
            model.kind = Shown::UpToDate;
            model.text = "Panda is up to date";
            break;
        case Kind::Updated:
            model.kind = Shown::Updated;
            model.text = "Updated to " + state.version + " · Release notes";
            model.clickable = true;
            break;
        case Kind::Error:
            model.kind = Shown::Error;
            model.text = "Update failed · Details";
            model.clickable = true;
            break;
    }
    return model;
}

std::string EditorLayer::updateStatusLine() const {
    using Kind = UpdateState::Kind;
    const UpdateState &state = m_updater.state();
    std::string line = "Panda " + EditorUpdater::versionString();
    if (ENGINE_BUILD_COMMIT[0] != '\0') { line += std::string(" (commit ") + ENGINE_BUILD_COMMIT + ")"; }
    switch (state.kind) {
        case Kind::Idle: return line;
        case Kind::Checking: return line + ". Checking for updates…";
        case Kind::Downloading: return line + ". Downloading " + state.version + "…";
        case Kind::Installing: return line + ". Installing " + state.version + "…";
        case Kind::Ready: return line + ". Update " + state.version + " is installed and runs after a restart.";
        case Kind::UpToDate: return line + ". Up to date.";
        case Kind::Updated: return line + ". Updated to " + state.version + ".";
        case Kind::Error: return line + ". Update failed: " + state.message;
    }
    return line;
}

void EditorLayer::pushUpdateStatus() {
    const UpdateState &state = m_updater.state();
    const bool dismissed = m_updater.isDismissed();
    if (state.revision == m_shownUpdateRevision && dismissed == m_shownUpdateDismissed) { return; }
    m_shownUpdateRevision = state.revision;
    m_shownUpdateDismissed = dismissed;
    const Editor::UpdateIndicatorModel model = updateIndicatorModel();
    if (m_editorController) { m_editorController->setUpdateStatus(model); }
    if (m_startScreenController) { m_startScreenController->setUpdateStatus(model); }
    const bool ready = m_updater.hasReadyUpdate();
    if (ready != m_shownUpdateReady) {
        m_shownUpdateReady = ready;
        m_menuBar.rebuild(); // "Restart to Update" appears and goes with the installed update
    }
}

void EditorLayer::menuBarExportGame() {
    cookRuntimeData();
}

void EditorLayer::menuBarOpenExportProject(ExportProjectTarget target) {
    openExportProject(target);
}

void EditorLayer::startScreenDidRequestOpenProjectDialog() {
    std::optional<path_t> optionalPath = SystemTools::openFolderDialog();
    if (!optionalPath.has_value()) {
        if (m_startScreenController) { m_startScreenController->setStatus("Project opening cancelled."); }
        return;
    }

    m_loader.openProject(optionalPath.value());
    if (!m_loader.hasOpenedProject() && m_startScreenController) {
        m_startScreenController->setStatus("Could not open project: " + optionalPath->string());
    }
}

void EditorLayer::startScreenDidRequestOpenRecentProject(size_t index) {
    const std::vector<RecentProject> &recentProjects = m_loader.getRecentProjectsList();
    if (index >= recentProjects.size()) { return; }

    const path_t projectPath = recentProjects[index].path;
    if (!std::filesystem::exists(projectPath)) {
        if (m_startScreenController) { m_startScreenController->setStatus("Project not found: " + projectPath.string()); }
        return;
    }

    m_loader.openProject(projectPath);
    if (!m_loader.hasOpenedProject() && m_startScreenController) {
        m_startScreenController->setStatus("Could not open project: " + projectPath.string());
    }
}

void EditorLayer::startScreenDidRequestRemoveRecentProject(size_t index) {
    const std::vector<RecentProject> &recentProjects = m_loader.getRecentProjectsList();
    if (index >= recentProjects.size()) { return; }

    const std::string name = recentProjects[index].name.empty()
                                 ? path_t(recentProjects[index].path).filename().string()
                                 : recentProjects[index].name;
    m_loader.removeRecentProject(static_cast<int>(index));
    m_loader.saveSession();
    syncStartScreenModel();
    if (m_startScreenController) { m_startScreenController->setStatus("Removed " + name + " from recent projects."); }
}

void EditorLayer::startScreenDidRequestCreateProject(const std::string &name, const std::string &location) {
    const std::string projectName = name.empty() ? "UntitledGame" : name;
    const path_t parentPath = location.empty() ? defaultProjectParentPath() : path_t(location);
    const path_t projectPath = parentPath / projectName;

    if (std::filesystem::exists(projectPath)) {
        if (m_startScreenController) { m_startScreenController->setStatus("Project already exists: " + projectPath.string()); }
        return;
    }

    m_loader.createProject(projectName, projectPath);
    if (!m_loader.hasOpenedProject() && m_startScreenController) {
        m_startScreenController->setStatus("Could not create project: " + projectPath.string());
    }
}

void EditorLayer::startScreenDidRequestChooseProjectLocation() {
    const std::string initialFolder = defaultProjectParentPath().string();
    std::optional<path_t> optionalPath = SystemTools::openFolderDialog(initialFolder.c_str());
    if (!optionalPath.has_value()) { return; }
    if (m_startScreenController) { m_startScreenController->setNewProjectLocation(optionalPath->string()); }
}

bool EditorLayer::cookRuntimeData() {
    if (!m_loader.hasOpenedProject()) {
        LOG_WARN_EDITOR("No project is open. Open a project before exporting.");
        return false;
    }

    const ProjectModuleSettings &modules = m_loader.projectSettings().get<ProjectModuleSettings>();
    AssetHandlerEditor *assetHandler = static_cast<AssetHandlerEditor *>(GameContext::getAssetHandler());

    // Validate before exporting. The export cooks the open world (GameContext::getCurrentWorld);
    // the start world of the project settings takes over with the scene list (context/roadmap.md).
    std::string openWorld;
    if (!m_loader.getOpenedWorldPath().empty()) {
        openWorld = std::filesystem::relative(m_loader.getOpenedWorldPath(), m_loader.getOpenedProjectPath()).generic_string();
    }
    std::vector<std::string> validationErrors =
        GameExporter::validate(m_loader.getOpenedProjectPath(), openWorld, *assetHandler);

    if (!validationErrors.empty()) {
        for (const auto &error : validationErrors) {
            LOG_ERROR_EDITOR("EXPORT VALIDATION: %s", error.c_str());
        }
        LOG_ERROR_EDITOR("Export validation failed. Fix the errors above before exporting.");
        return false;
    }

    path_t runtimeDataDir = m_loader.getOpenedProjectPath() / ".Panda" / "Cooked" / "Runtime";
    path_t exportProjectDir = m_loader.getOpenedProjectPath() / "Export";

    LOG_INFO_EDITOR("========================================");
    LOG_INFO_EDITOR("Cooking runtime data to: %s", runtimeDataDir.string().c_str());
    LOG_INFO_EDITOR("========================================");

    ExportSettings exportSettings;
    exportSettings.runtimeDataDirectory = runtimeDataDir;
    exportSettings.appName = m_loader.getOpenedProjectPath().filename().string();
    exportSettings.modules = ProjectModules::enabledIds(modules);

    World *world = GameContext::getCurrentWorld();

    GameExporter exporter;
    ExportResult result = exporter.exportGame(exportSettings, m_loader.getOpenedProjectPath(), *assetHandler, *world);

    if (result.success) {
        LOG_INFO_EDITOR("========================================");
        LOG_INFO_EDITOR("Runtime data cooked successfully!");
        LOG_INFO_EDITOR("Runtime data: %s", runtimeDataDir.string().c_str());
        LOG_INFO_EDITOR("Export project: %s", exportProjectDir.string().c_str());
        LOG_INFO_EDITOR("========================================");
        for (const auto &warning : result.warnings) {
            LOG_WARN_EDITOR("EXPORT WARNING: %s", warning.c_str());
        }
        return true;
    } else {
        LOG_ERROR_EDITOR("Export failed: %s", result.errorMessage.c_str());
        return false;
    }
}

void EditorLayer::openExportProject(ExportProjectTarget target) {
    if (!cookRuntimeData()) { return; }

    const ExportProjectOpenCommand command = exportProjectOpenCommand(target);
    const char *buildDirName = ProjectBuilder::exportBuildDirName(target);
    const path_t projectPath = m_loader.getOpenedProjectPath();
    const path_t exportPath = projectPath / "Export";
    const path_t buildPath = exportPath / "build" / buildDirName;
    const std::optional<path_t> cmakePath = SystemTools::findCMakeExecutable();
    if (!cmakePath.has_value()) {
        LOG_ERROR_EDITOR("CMake executable not found");
        return;
    }
    const std::optional<path_t> sdkDir = findPandaSdkConfigDirectory();
    if (!sdkDir.has_value()) {
        LOG_ERROR_EDITOR("PandaSDK not found near the editor");
        return;
    }

    LOG_INFO_EDITOR("Configuring export project: %s", buildDirName);
    LOG_INFO_EDITOR("CMake: %s", cmakePath->string().c_str());
    // The same argument composition as in the CMake panel (without presets — they carried
    // machine-specific paths).
    if (!SystemTools::runCommand(
            *cmakePath, ProjectBuilder::exportConfigureArgs(target, exportPath, *sdkDir), {}, logCommandOutput
        )) {
        LOG_ERROR_EDITOR("Failed to configure export project: %s", buildDirName);
        return;
    }

    std::optional<path_t> generatedProject = findGeneratedProjectFile(buildPath, command.generatedProjectExtension);
    if (!generatedProject.has_value()) {
        LOG_ERROR_EDITOR("%s project not found in: %s", command.projectTypeName, buildPath.string().c_str());
        return;
    }

    if (SystemTools::openWithDefaultApplication(*generatedProject)) {
        LOG_INFO_EDITOR("Opened %s project: %s", command.projectTypeName, generatedProject->string().c_str());
    } else {
        LOG_ERROR_EDITOR("Failed to open %s project: %s", command.projectTypeName, generatedProject->string().c_str());
    }
}

#pragma endregion

void EditorLayer::openProjectFolderDialog() {
    std::optional<path_t> optionalPath = SystemTools::openFolderDialog();
    if (!optionalPath.has_value()) { return; }
    path_t path = optionalPath.value();
    saveOpenedProjectWindowState();
    m_loader.openProject(path);
}

void EditorLayer::updateWindowState() {
    if (m_loader.hasOpenedProject()) {
        m_window->setFlags(FERN_WINDOW_FLAGS_DEFAULT);
        auto windowState = m_loader.getLastWindowState();
        if (windowState.isFullScreen) {
            m_window->setState(Fern::WindowState::WindowStateFullScreen);
        } else {
            // Normal size first (even before maximizing): "restore" from maximized after a
            // restart brings back the previous window bounds.
            if (windowState.isSizeValid()) {
                m_window->setState(Fern::WindowState::WindowStateNormal);
                m_window->setSize({(float)windowState.width, (float)windowState.height});
            }
            if (windowState.isMaximized || !windowState.isSizeValid()) {
                m_window->setState(Fern::WindowState::WindowStateMaximized);
            }
        }
    } else {
        m_window->setFlags(FERN_WINDOW_FLAG_CLOSABLE);
        m_window->setTitle("Welcome");
        m_window->setState(Fern::WindowState::WindowStateNormal);
        m_window->setSize({800, 600});
    }
}

bool EditorLayer::saveWorld() {
    // Save always writes the world being edited. Play and Simulate run on its copy: what the
    // simulation and the inspector change there lives until Stop and is not saved.
    const bool saved = m_loader.saveWorld();
    if (saved) {
        if (m_sceneState == SceneState::EDIT) {
            LOG_INFO_EDITOR("World saved.");
        } else {
            LOG_INFO_EDITOR("World saved. The changes made while playing are not saved: Stop discards them.");
        }
    }
    // Modified assets (materials/postprocessings) are saved by the same Save (Cmd+S) — even if the
    // world itself has not changed.
    if (auto *assetHandler = static_cast<AssetHandlerEditor *>(GameContext::getAssetHandler())) {
        const int savedAssets = assetHandler->documents().saveChangedAssets();
        if (savedAssets > 0) { LOG_INFO_EDITOR("Saved %d assets.", savedAssets); }
    }
    // The project settings document, when its tab has unsaved edits.
    if (m_editorController && !m_editorController->saveProjectDocuments()) {
        LOG_ERROR_EDITOR("Project settings could not be saved.");
        return false;
    }
    return saved;
}

void EditorLayer::closeProject() {
    saveOpenedProjectWindowState();
    m_thumbnails->clear(); // project assets die — previews/thumbnails must not hold ids
    m_loader.closeProject();
}

void EditorLayer::closeAppRequest() {
    runWithSaveCurrentWorldDialog([this]() { closeApp(); });
}

void EditorLayer::closeApp() {
    saveOpenedProjectWindowState();
    Application::get()->close();
}

void EditorLayer::restartEditor() {
    // The same flow as Quit, the unsaved-changes dialog included; main() starts the new instance
    // once this one has written its settings and torn the window down.
    runWithSaveCurrentWorldDialog([this]() {
        SystemTools::setRelaunchOnExit(true);
        closeApp();
    });
}

void EditorLayer::saveOpenedProjectWindowState() {
    if (!m_loader.hasOpenedProject()) { return; }

    Fern::WindowState windowState = m_window->getState();
    // The base is the previously saved values: the size is updated only from the Normal state,
    // otherwise the size of a maximized/fullscreen window leaks into the config.
    LastOpenedProjectWindowState savingState = m_loader.getLastWindowState();
    savingState.isFullScreen = windowState == Fern::WindowState::WindowStateFullScreen;
    savingState.isMaximized = windowState == Fern::WindowState::WindowStateMaximized;
    if (windowState == Fern::WindowState::WindowStateNormal) {
        savingState.width = m_window->getSize().width;
        savingState.height = m_window->getSize().height;
    }
    m_loader.saveWindowState(savingState);
    if (m_editorController) { m_loader.saveDockLayout(m_editorController->captureDockLayout()); }
}

void EditorLayer::runWithSaveCurrentWorldDialog(std::function<void()> action) {
    if (m_sceneState != SceneState::EDIT) { stop(); }
    auto *assetHandler = static_cast<AssetHandlerEditor *>(GameContext::getAssetHandler());
    const std::vector<AssetId> dirtyAssets =
        assetHandler ? assetHandler->documents().collectChangedAssets() : std::vector<AssetId>{};
    const std::vector<std::string> dirtyDocuments =
        m_editorController ? m_editorController->dirtyProjectDocumentTitles() : std::vector<std::string>{};
    if (!m_editingWorld.isChanged() && dirtyAssets.empty() && dirtyDocuments.empty()) {
        if (action) { action(); }
        return;
    }

    // The question names WHAT is unsaved: the world, a specific asset or document by name, or
    // their count.
    std::vector<std::string> items;
    if (m_editingWorld.isChanged()) { items.push_back("current world"); }
    if (dirtyAssets.size() == 1) {
        items.push_back("\"" + assetHandler->getAssetName(dirtyAssets.front()) + "\"");
    } else if (!dirtyAssets.empty()) {
        items.push_back(std::to_string(dirtyAssets.size()) + " modified assets");
    }
    for (const std::string &document : dirtyDocuments) { items.push_back(document); }
    std::string title = "Save ";
    for (size_t i = 0; i < items.size(); i++) {
        if (i > 0) { title += i + 1 == items.size() ? " and " : ", "; }
        title += items[i];
    }
    title += "?";
    const SystemTools::SaveChangesChoice choice =
        SystemTools::showSaveChangesDialog(title.c_str(), "Do you want to save your changes?");
    if (choice == SystemTools::SaveChangesChoice::Cancel) { return; }
    if (choice == SystemTools::SaveChangesChoice::Save && !saveWorld()) { return; } // Save-As cancelled → stop
    if (action) { action(); }
}

// Status indicators of the visibility strip: process RAM/VRAM, CPU load (fraction of a core,
// like top) and GPU (GPU frame time over wall-clock frame time). Sampled every half second —
// the metrics are cheap, but there is no point in polling them every frame, and running numbers
// make reading harder.
void EditorLayer::updateStripStatus(double deltaTime) {
    m_stripStatusTimer += deltaTime;
    if (m_stripStatusTimer < 0.5) { return; }
    const double interval = m_stripStatusTimer;
    m_stripStatusTimer = 0.0;
    if (!m_editorController) { return; }

    const Editor::SystemStats system = Editor::sampleSystemStats();
    const Miren::GpuStats gpu = Miren::getGpuStats();
    const double frameMs = interval * 1000.0 / std::max(1, m_stripFrameCounter);
    const double gpuPercent =
        frameMs > 0.01 ? std::clamp(gpu.frameMs / frameMs * 100.0, 0.0, 999.0) : 0.0;
    m_stripFrameCounter = 0;

    const auto gigabytes = [](uint64_t bytes) { return (double)bytes / (1024.0 * 1024.0 * 1024.0); };
    char text[128];
    snprintf(
        text, sizeof(text), "MEM %.1fG   VRAM %.1fG   CPU %.0f%%   GPU %.0f%%",
        gigabytes(system.residentBytes), gigabytes(gpu.allocatedBytes), system.cpuPercent,
        gpuPercent
    );
    m_editorController->setStripStatusText(text);
}

} // namespace Panda
