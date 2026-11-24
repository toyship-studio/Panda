#include "EditorLayer.hpp"
#include "App/EditorMcp.hpp"
#include "UI/Settings/CoreSettingsSections.hpp"

#include "Project/Loader/AssetHandlerEditor.hpp"
#include "Project/Modules/ProjectModules.hpp"
#include "Project/System/SystemColorSampler.hpp"
#include "Project/System/SystemTools.hpp"
#include "UI/EditorDialogs.hpp"

#include <MirenTools.hpp>

#include <Panda/Assets/Base/AssetImporterBase.hpp>
#include <Panda/Renderer/SceneWind.hpp>
#include <Panda/Renderer/ShaderInterface/ShaderInterface.hpp>
#include <Panda/GameLogic/GameContext.hpp>
#include <Panda/GameLogic/Input.hpp>
#include <Panda/Serialization/SettingsMapper.hpp>
#include <Foundation/GraphicsAPI.hpp>
#include <Panda/Base/Version.hpp>
#include <Foundation/PlatformDetection.hpp>
#include <Fern/Fonts/FontCatalog.hpp>
#include <Fern/Fern.hpp>
#include <Fern/Files/AtomicWrite.hpp>
#include <Fern/Events/EventQueue.hpp>
#include <Fern/Events/WindowEvents.hpp>
#include <PandaUI/PandaUI.hpp>

#include "UI/EditorController.hpp"
#include "UI/WorldTabs.hpp"
#include <PandaEditorKit/EditorExtensions.hpp>
#include <PandaEditorKit/EditorHost.hpp>
#include <PandaEditorKit/Selection.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <utility>

namespace Panda {

namespace {

    // The window of an unattended editor (--unattended), in points: it fits the screen of a laptop.
    constexpr float UNATTENDED_WINDOW_WIDTH = 1280.f;
    constexpr float UNATTENDED_WINDOW_HEIGHT = 800.f;

    // The input of the window: EditorLayer::onEvent gives every such event either to the game or
    // to the editor chrome, and nothing else of it reaches Panda::Input.
    bool isInputEvent(const Fern::Event *event) {
        switch (event->type) {
            case Fern::EventType::KeyPressed:
            case Fern::EventType::KeyReleased:
            case Fern::EventType::InputCharacter:
            case Fern::EventType::MouseMoved:
            case Fern::EventType::MouseButtonPressed:
            case Fern::EventType::MouseButtonReleased:
            case Fern::EventType::MouseScrolled:
            case Fern::EventType::TouchBegan:
            case Fern::EventType::TouchMoved:
            case Fern::EventType::TouchEnded:
                return true;
            default:
                return false;
        }
    }

    std::vector<std::string> titlesOf(const Editor::CreateActions &actions) {
        std::vector<std::string> titles;
        for (const Editor::CreateAction &action : actions) { titles.push_back(action.title); }
        return titles;
    }

    // The list is made again at the click: the folder of the Content Browser is the one of now.
    void runAction(const Editor::CreateActions &actions, size_t index) {
        if (index < actions.size()) { actions[index].run(); }
    }

} // namespace

EditorLayer::EditorLayer(Fern::Window *window, EditorLaunch launch)
    : m_play(window, m_viewports)
    , m_documents(this)
    , m_loader(m_documents, this)
    , m_documentActions(*this)
    , m_diskWatch(*this)
    , m_builds(m_loader, m_documents, [this] { scriptsWereBuilt(); })
    , m_window(window)
    , m_menuBar(window, this, m_commands)
    , m_viewports()
    , m_appearance(m_editorUI, m_pluginStore, m_documents) {
    m_launch = std::move(launch);
    if (m_launch.unattended) { m_unattended = std::make_unique<SystemTools::Unattended>(); }
    registerCommands();
    m_commands.addKeymapListener([this] { m_menuBar.rebuild(); }); // the menu shows the keys
}

EditorLayer::~EditorLayer() = default; // EditorController and EditorMcp are complete here (see the includes) — for unique_ptr

/* ------------------------------ The life of the application ------------------------------ */

void EditorLayer::onAttach() {
    Foundation::EditorLogger::init(Editor::OutputPanelView::loggerCallback); // engine logs → Output panel
    // The engine reads the settings of the project from the store of project.json: an edit of a
    // section (the shadow maps, the distances of the terrain) applies at once, Save writes it.
    GameContext::setProjectSettings(&m_documents.projectSettingsFile().settings());
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
    Editor::selection().setWorld(shownWorld()); // the selection is of the world the editor shows
    m_play.initialize();
    m_viewports.init();
    m_viewports.scene().setWorld(shownWorld());
    m_viewports.cameraController().setPosition({0.f, 0.f, 4.f});
    // A project of the launch that is not one: the start screen says why (it is built below).
    const std::string notOpened = m_loader.loadInitialData(m_launch.openLastProject, m_launch.project);
    // A world applies its wind itself when its frame begins. Here the wind of the loaded world
    // goes into SceneWind BEFORE the first frame and before the UI is built: the material tabs of
    // the layout build their forms in applyDockLayout and must show the value of the system
    // fields (windStrength/windGustiness) the world blows, not the default.
    SceneWind::apply(editingWorld().wind());
    if (!m_loader.hasOpenedProject()) { updateWindowState(); }
    // The user's keys, next to the editor's settings.json; the menu is rebuilt by the listener.
    const path_t settingsPath = m_documents.editorSettingsFile().path();
    m_commands.loadKeymap(settingsPath.empty() ? path_t() : settingsPath.parent_path() / "keymap.json");
    // The files of the editor and of the project that is open, followed from here on.
    m_diskWatch.watch(m_loader.hasOpenedProject() ? m_loader.getOpenedProjectPath() : path_t());

    // PandaUI host of the editor chrome (the last pass of a frame, into the window). Fonts and
    // icons go BEFORE the controller is created (panels take icons from the global registry while
    // being built). The coordinator is Editor::EditorController.
    installSystemColorSampler(); // ColorPicker eyedropper (macOS: NSColorSampler; otherwise no-op)
    m_editorUI.initialize(m_window);
    m_editorUI.loadFonts(m_appearance.fonts());
    // The list of installed fonts for the APPEARANCE section is scanned once, off the main thread.
    Application::get()->getScheduler().submit(
        PandaAsync::JobDesc{"fonts.scan", PandaAsync::Priority::Background}, [](PandaAsync::Context &) { Fern::Fonts::families(); }
    );
    m_editorUI.loadEditorIcons();
    // Before the controller: panels read the theme and take their icons while being built.
    m_appearance.load();
    // The pictures of assets (previews of the forms, thumbnails): a single owner, the panels go
    // through AssetPreviews::shared().
    m_previews = std::make_unique<AssetPreviews>(
        [this](Miren::TextureHandle native, PandaUI::Size size) { return m_editorUI.wrapNativeTexture(native, size); },
        [this](PandaUI::TextureHandle handle) { m_editorUI.destroyTexture(handle); }
    );
    AssetPreviews::setShared(m_previews.get());
    m_startScreenController =
        std::make_unique<Editor::StartScreenController>(m_editorUI.loadImage("ui/start/Logo.png"), m_loader, this);
    m_editorController = std::make_unique<Editor::EditorController>(*this);
    m_editorController->setWorld(panelsWorld()); // panels read the real engine world directly
    showDocumentName(); // the world could load before the controller
    // A ready thumbnail → targeted update of the visible grid cell (cells are configured once).
    m_previews->setOnThumbnailReady([this](Panda::AssetId assetId) {
        if (m_editorController) { m_editorController->thumbnailDidBecomeReady(assetId); }
    });
    m_editorUI.context().setUnhandledKeyDownHandler(
        [this](const PandaUI::KeyEvent &event) {
            if (!m_loader.hasOpenedProject() || !m_editorController) { return false; }
            return m_commands.handleKey(event);
        }
    );
    m_menuBar.rebuild(); // File → New lists what the controller can create
    // The themes for the APPEARANCE section of the editor settings (the data plugins live here).
    Editor::setThemeProvider({
        [this] { return m_appearance.themeNames(); },
        [this] { return m_appearance.currentThemeIndex(); },
        [this](int index) { m_appearance.selectThemeIndex(index); },
    });
    // The updater for the UPDATES section of the editor settings.
    Editor::setUpdateProvider({
        EditorUpdater::isEnabled(),
        [this] { checkForUpdates(true); },
        [this] { return m_updater.statusLine(); },
    });
    loadEditorParts(); // parts host: the coordinator itself (since its creation) // project open at startup: module parts before layout (tab forms wait for them)
    m_editorController->applyDockLayout(m_loader.getDockLayout()); // saved dock layout (if any)
    m_viewportSettings = m_documents.editorSettings().viewport;
    m_viewports.cameraController().setMoveSpeed(m_viewportSettings.cameraSpeed);
    if (m_loader.hasOpenedProject()) {
        showEditorChrome();
    } else {
        if (!notOpened.empty()) { m_startScreenController->setStatus(notOpened); }
        showStartScreen();
    }
    // After the settings and the chrome: the first check runs at once when automatic updates are on.
    // Nobody is at the screen: the run is the same whatever was published since.
    if (!m_launch.unattended) { m_updater.start(m_documents.editorSettings().updates.autoUpdate); }
    m_pluginStore.start(m_documents.editorSettings().updates.autoUpdate); // plugin updates follow the same switch
    m_mcp = std::make_unique<EditorMcp>(*this); // listens once a project with the "mcp" section on is open
}

void EditorLayer::onDetach() {
    // The files nobody asked to write, while the editor still answers (the camera of the active
    // world goes into the session).
    m_loader.saveOnExit();
    GameContext::setProjectSettings(nullptr);
    m_mcp.reset();
    Fern::setCursorLocked(false);
    AssetPreviews::setShared(nullptr);
    m_previews.reset();
    m_editorUI.shutdown();
    m_play.shutdown();
    Editor::selection().clear();
    Editor::selection().setWorld(nullptr);
}

/* ------------------------------ The frame ------------------------------ */

void EditorLayer::onUpdate(double deltaTime) {
    syncWindowIdentity(deltaTime);
    m_statistics.updateStatus(deltaTime, m_editorController.get(), m_documents.editorSettings().appearance.showSystemStatus);
    m_updater.tick(deltaTime);
    pushUpdateStatus();
    if (m_play.takeQuitRequest()) { stop(); } // a script quit in the last frame: Stop before the worlds update
    if (m_scriptsBuiltWhilePlaying && !isPlaying()) {
        m_scriptsBuiltWhilePlaying = false;
        if (m_loader.hasOpenedProject()) { m_loader.reloadScriptsDll(); }
    }
    // The requests of the agent, before the frame is built: a tool changes the state of the
    // editor (Play, Reload, another world) where a click of the user would.
    if (m_mcp) { m_mcp->update(); }
    // What changed on disk by itself, taken at the same place — while the editor is the
    // application in front: one of its windows has the keyboard. An unattended editor is in
    // front when a test says so (EditorMcp), whatever its window does.
    if (!m_launch.unattended) {
        Fern::Window *second = m_editorUI.auxiliaryWindow();
        m_diskWatch.setInFront(m_window->isFocused() || (second != nullptr && second->isFocused()));
    }
    m_diskWatch.update(deltaTime);
    // Size the engine viewports from the PandaUI panels BEFORE the world renders this frame.
    if (m_loader.hasOpenedProject()) {
        syncViewports();
        m_play.updateInput(m_editorController->isGameViewportFocused(), m_editorUI.context().modals().isPresenting());
        m_viewports.scene().pollSelection(); // apply async rect-select / click-picking results
        m_editorController->syncSelection(); // what the viewport selected → the panels and the tabs
        m_editorController->syncWorldStructure(); // scripts created/deleted entities → live Hierarchy
        m_editorController->updateChromeFromState(!isPlaying()); // toolbar + dirty scene tab
        m_builds.update(); // the output of cmake → the CMake panel; built scripts are loaded
        followBuildBeforePlay();
    }

    if (m_loader.hasOpenedProject()) {
        // The frame of the world on screen (the playing copy in Play) through the live viewport: the
        // Scene sees it with the editor camera, the Game with the main camera of the world.
        const bool playing = isPlaying();
        World &world = *shownWorld();
        // What the user did over the Scene surface since the last frame: the flight of the editor
        // camera, the pointer of the gizmo and of the tools. Taken every frame (the surface
        // accumulates), meaningful only while the Scene renders.
        const Editor::SceneNavigation sceneNavigation = m_editorController->takeSceneNavigation();
        m_scenePointer = m_editorController->takeScenePointer();
        CameraController::Navigation navigation;
        if (m_viewports.isSceneLive()) {
            navigation.forward = sceneNavigation.forward;
            navigation.backward = sceneNavigation.backward;
            navigation.left = sceneNavigation.left;
            navigation.right = sceneNavigation.right;
            navigation.blocked = sceneNavigation.blocked;
            navigation.rotate = glm::vec2(sceneNavigation.rotate.x, sceneNavigation.rotate.y);
            navigation.zoom = sceneNavigation.zoom;
        } else {
            m_scenePointer = Editor::ScenePointer{};
        }
        m_viewports.beginFrame(world, deltaTime, m_viewportSettings, navigation);
        if (!playing) {
            updateViewportTools(deltaTime); // brushes — before the frame is drawn: edits are remeshed this frame
        }
        // The game is simulated, then the world is drawn through the live viewport — while that
        // one is on screen: behind the tab of a material nobody looks at the world, and the game
        // goes on without its picture. A playing world that does not run (it waits for its next
        // world) keeps its last frame.
        if (playing && m_play.isRunning()) { m_play.simulate(deltaTime); }
        if (m_viewports.isOnScreen() && (!playing || m_play.isRunning())) {
            m_viewports.render(world, m_viewportSettings, Application::get()->getTime());
        }
        if (playing) { m_play.updateUi(deltaTime); } // the UI of the game over its frame; the world a script asked for
        updateViewportHelperLines(); // after the camera update — collider outlines without a one-frame lag
        m_previews->update();        // one preview of a form + one thumbnail from the queue
        GameContext::getAssetHandler()->cleanup();
    }

    if (m_loader.hasOpenedProject()) {
        m_statistics.updateFrame(deltaTime, m_viewports, m_editorUI, *m_editorController);
        if (Editor::editorExtensions().viewportTools.anyDriving()) {
            // The active viewport tool takes the LMB: the gizmo does not manipulate and hides.
            m_editorController->setGizmoVisible(false);
            m_editorController->setGizmoActive(false);
        } else {
            updateGizmo(); // gizmo: input+manipulation BEFORE the overlay renders
        }
    }
    m_editorUI.update(deltaTime); // PandaUI chrome on top of the scene
    updatePace(deltaTime);
    updateBackups(deltaTime);
}

// A copy of the unsaved edits is a serialization of each changed world on this thread (a world
// cannot be read elsewhere while it is edited), and its cost grows with the world: 20 000 entities
// take 1.8 s in a debug build. So it waits for a pause of the input — never falling into a gesture —
// and what the last copy cost decides how long: at least ten seconds apart and at most a hundredth
// of the time, the pause three times as long as the copy. A cheap one (under 50 ms, a frame or
// three) comes within a minute even to a user who never pauses; an expensive one waits for the
// pause however long. Nothing is written when nothing changed (FileDocuments::backUp). Play and a
// script build loaded make one at once, whatever it costs: the game code runs in this process, and
// a crash of it takes the editor along.
void EditorLayer::updateBackups(double deltaTime) {
    constexpr double EVERY = 10.0;
    constexpr double PAUSE = 2.0;
    constexpr double LATEST = 60.0;
    constexpr double CHEAP = 0.05;
    if (!m_loader.hasOpenedProject()) { return; }
    if (!m_backupsAsked && m_editorController && !m_editorUI.context().modals().isPresenting()) {
        m_backupsAsked = true;
        const std::vector<std::string> left = m_documents.leftBackups();
        if (!left.empty()) {
            Editor::showRestoreDialog(
                &m_editorUI.context(), left,
                [this, count = left.size()] {
                    m_documents.restoreLeftBackups();
                    LOG_INFO_EDITOR("Restored the unsaved changes of %zu %s.", count, count == 1 ? "document" : "documents");
                },
                [this] { m_documents.discardLeftBackups(); }
            );
        }
    }
    m_inputPause += deltaTime;
    m_backupWait += deltaTime;
    const double every = std::max(EVERY, m_backupCost * 100.0);
    const double pause = std::max(PAUSE, m_backupCost * 3.0);
    const bool forced = m_backupCost < CHEAP && m_backupWait >= LATEST;
    if (m_backupWait < every || (m_inputPause < pause && !forced)) { return; }
    m_backupWait = 0.0;
    const auto begun = std::chrono::steady_clock::now();
    if (m_documents.backUp() > 0) {
        m_backupCost = std::chrono::duration<double>(std::chrono::steady_clock::now() - begun).count();
    }
}

// The editor rests when nobody looks at it and nothing goes on: behind another application (an
// unattended one is in front when a test says so — EditorMcp, `focus`), not playing, no build, no
// background job delivering, no agent talking to it for two seconds. Then it draws ten frames a
// second instead of the display's rate — what changes still shows, a tenth of a second late — and
// an event of its windows (the click that brings it back) ends the rest at once. In front it
// draws every frame: the materials that move with time move smoothly, and nothing waits.
void EditorLayer::updatePace(double deltaTime) {
    constexpr double QUIET_BEFORE_REST = 2.0; // seconds: a burst of work does not flicker in and out of rest
    constexpr double REST_INTERVAL = 0.1;
    const uint64_t mcpRequests = m_mcp ? m_mcp->handledRequests() : 0;
    const bool agent = mcpRequests != m_mcpRequests;
    m_mcpRequests = mcpRequests;
    const bool busy = m_diskWatch.isInFront() || isPlaying() || m_builds.isRunning() ||
                      Application::get()->deliveredCompletions() || agent;
    m_quietSeconds = busy ? 0.0 : m_quietSeconds + deltaTime;
    Application::get()->setRestInterval(m_quietSeconds > QUIET_BEFORE_REST ? REST_INTERVAL : 0.0);
}

void EditorLayer::syncViewports() {
    if (!m_editorController) { return; }
    const Editor::ViewportSurfaces surfaces = m_editorController->viewportSurfaces();
    ViewportFrames frames;
    frames.scene = Rect(surfaces.scene.origin.x, surfaces.scene.origin.y, surfaces.scene.size.width, surfaces.scene.size.height);
    frames.game = Rect(surfaces.game.origin.x, surfaces.game.origin.y, surfaces.game.size.width, surfaces.game.size.height);
    frames.shown = surfaces.showsGame ? ViewportKind::Game : ViewportKind::Scene;
    frames.visible = surfaces.visible;
    m_viewports.sync(frames);
    // Wrap the output textures once (the native handles are stable across resizes) and feed the panels.
    // The wrap waits for a laid-out tab: an ImageView draws nothing for a texture of size zero, and the
    // size is frozen at the wrap (the panel stretches the image to its frame anyway).
    // GL renders FBO textures bottom-left origin (needs flip), D3D — top-left.
    const bool flip = Foundation::ACTIVE_GRAPHICS_TRAITS.originBottomLeft;
    const auto laidOut = [](const PandaUI::Rect &frame) { return frame.size.width >= 1.f && frame.size.height >= 1.f; };
    const Miren::TextureHandle sceneNative = m_viewports.sceneResult();
    if (sceneNative.isValid() && sceneNative.id != m_sceneNative.id && laidOut(surfaces.scene)) {
        m_sceneTexture = m_editorUI.wrapNativeTexture(sceneNative, PandaUI::Size(surfaces.scene.size.width, surfaces.scene.size.height));
        m_sceneNative = sceneNative;
    }
    if (m_sceneTexture.isValid()) { m_editorController->setViewportSceneTexture(m_sceneTexture, flip); }
    const Miren::TextureHandle gameNative = m_viewports.gameResult();
    if (gameNative.isValid() && gameNative.id != m_gameNative.id && laidOut(surfaces.game)) {
        m_gameTexture = m_editorUI.wrapNativeTexture(gameNative, PandaUI::Size(surfaces.game.size.width, surfaces.game.size.height));
        m_gameNative = gameNative;
    }
    if (m_gameTexture.isValid()) { m_editorController->setGameViewportTexture(m_gameTexture, flip); }
    m_editorController->setGameViewportMessage(
        !hasWorld() && !isPlaying()                                ? "No world is open"
        : m_viewports.isGameLive() && !m_viewports.gameHasCamera() ? "No main camera in the world: the editor camera is shown"
                                                                   : ""
    );
}

void EditorLayer::postGameEvent(Fern::Event &event) {
    if (!isPlaying()) { return; }
    if (!m_play.handleEvent(&event)) { Input::onEvent(&event); }
}

void EditorLayer::updateViewportTools(double deltaTime) {
    // The tools work in the Scene with the editor camera and the pointer of its surface; in a Game
    // frame they see no viewport (the pointer is empty).
    const Editor::ViewportToolFrame frame{
        .world = editingWorld(),
        .camera = m_viewports.editorCamera(),
        .view = m_viewports.sceneView(),
        .viewProj = m_viewports.sceneViewProj(),
        .cameraPosition = m_viewports.cameraController().getPosition(),
        .viewportHovered = m_scenePointer.hovered,
        .pointerPosition = glm::vec2(m_scenePointer.local.x, m_scenePointer.local.y),
        .pointerDown = m_scenePointer.leftDown,
        .deltaTime = deltaTime,
    };
    Editor::ViewportToolRegistry &tools = Editor::editorExtensions().viewportTools;
    for (const std::unique_ptr<Editor::ViewportTool> &tool : tools.tools()) { tool->update(frame); }
    m_editorController->setViewportToolCapture(tools.anyDriving());
}

void EditorLayer::updateViewportHelperLines() {
    // Helper lines are drawn with the editor camera over the world the Scene shows (the editing
    // world, or the playing copy in Play — the runtime state lives there). In a Game frame the
    // Scene does not render and gets no lines.
    std::vector<HelperLine> lines;
    if (m_viewports.isSceneLive()) {
        // The overlay draws as many lines as its share of the UI draw list holds, so they go in the
        // order of importance: the lines of the tools (the feedback of the edit in progress), then
        // the overlays of the modules (collider outlines from the nearest one).
        const glm::mat4 &viewProj = m_viewports.sceneViewProj();
        const PandaUI::Rect frame = m_editorController->viewportSceneFrame();
        if (!isPlaying()) {
            for (const std::unique_ptr<Editor::ViewportTool> &tool : Editor::editorExtensions().viewportTools.tools()) {
                tool->appendHelperLines(lines, viewProj, frame);
            }
        }
        for (const Editor::ViewportOverlay &overlay : Editor::editorExtensions().viewportOverlays) {
            if (overlay.appendHelperLines != nullptr) {
                overlay.appendHelperLines(*shownWorld(), lines, viewProj, frame);
            }
        }
    }
    m_editorController->setViewportHelperLines(std::move(lines));
}

void EditorLayer::updateGizmo() {
    if (!m_editorController) { return; }
    // The gizmo lives in the Scene: in a Game frame there is nothing to manipulate in.
    if (!m_viewports.isSceneLive()) {
        m_editorController->setGizmoVisible(false);
        m_editorController->setGizmoActive(false);
        return;
    }
    // Gizmo input from the Scene surface (the pointer it recorded this frame). The position is in
    // window points (like the getDrawData coordinates). blockedByUi=false: clicks on the PandaUI
    // toolbar are consumed by PandaUI itself and never reach the surface.
    const Gizmo::InputState input{
        .mousePosition = Vec2(m_scenePointer.position.x, m_scenePointer.position.y),
        .mouseDown = m_scenePointer.leftDown,
        .mouseClicked = m_scenePointer.leftPressed,
        .viewportHovered = m_scenePointer.hovered,
        .blockedByUi = false,
    };
    // At runtime the physics moves the selected bodies by itself, so the selection median (the gizmo
    // center and the inspector's Transform fields) is refreshed once per frame; otherwise the gizmo
    // freezes at the body's starting position.
    if (shownWorld()->isRunning() && Editor::selection().hasEntities()) { Editor::selection().transformChanged(); }
    const glm::mat4 view = m_viewports.sceneView(); // the editor camera: the Scene manipulates in its screen rays
    Gizmos &gizmo = m_viewports.gizmo();
    gizmo.setSteps({m_viewportSettings.snapMove, m_viewportSettings.snapRotate, m_viewportSettings.snapScale});
    const bool visible = gizmo.manipulate(m_viewports.scene().getFrame(), input, &view, m_scenePointer.snap);
    m_editorController->setGizmoVisible(visible);
    m_editorController->setGizmoActive(gizmo.isHovered()); // hover over a handle / drag → takes LMB from rect-select
}

/* ------------------------------ The events of the window ------------------------------ */

void EditorLayer::onEvent(Fern::Event *event) {
    // Nobody is at the screen (--unattended): the mouse and the keyboard of the machine are not
    // the input of the editor or of the game. What a UI test sends goes straight into the UI
    // (EditorMcp), not through the window.
    if (m_launch.unattended && (isInputEvent(event) || event->type == Fern::EventType::FilesDropped)) {
        event->isHandled = true;
        return;
    }
    followDiskWatchEvent(*event);
    const bool modal = m_editorUI.context().modals().isPresenting();
    if (m_menuBar.handleEvent(event, modal)) { return; }

    // The second window (the Settings, the export of the game): its own UI tree takes its input;
    // closing it is the controller's (what the window shows is its).
    if (m_editorUI.auxiliaryWindow() != nullptr && event->getWindow() == m_editorUI.auxiliaryWindow()) {
        switch (event->type) {
            case Fern::EventType::WindowCloseRequest:
                if (m_editorController) { m_editorController->closeAuxiliaryWindow(); }
                break;
            case Fern::EventType::WindowFocus:
                if (m_editorController) { m_editorController->auxiliaryWindowFocusChanged(true); }
                break;
            case Fern::EventType::WindowLostFocus:
                if (m_editorController) { m_editorController->auxiliaryWindowFocusChanged(false); }
                break;
            default:
                m_editorUI.handleEvent(event);
                break;
        }
        event->isHandled = true;
        return;
    }

    if (event->type == Fern::EventType::FilesDropped) {
        if (event->getWindow() == m_window) {
            const auto *drop = static_cast<const Fern::FilesDroppedEvent *>(event);
            dropFiles(Fern::getEventQueue()->droppedFiles(*drop), PandaUI::Point(drop->x, drop->y));
        }
        event->isHandled = true;
        return;
    }

    // The focus and the minimizing of the editor window: the game follows them.
    m_play.windowEvent(*event);

    // The input of the window is either the game's or the editor's, never both. In Play, while
    // the Game is on screen, the game owns the pointer where the Game surface is what lies under
    // it — not a menu or a popup of the editor over the frame — and the keys while that surface
    // has the focus (PlaySession::ownsEvent): its UI sees them first, the rest Application feeds
    // to Panda::Input. Everything else is the editor's: the PandaUI chrome, whose World view
    // collects the camera navigation and the pointer of the gizmo and of the tools; none of it
    // reaches Panda::Input.
    if (isInputEvent(event)) {
        m_inputPause = 0.0;
        const auto overGame = [this](double x, double y) {
            return m_editorController->isGameUnderPointer(PandaUI::Point(static_cast<float>(x), static_cast<float>(y)));
        };
        if (isPlaying() && m_viewports.isGameOnScreen() &&
            m_play.ownsEvent(*event, m_editorController->isGameViewportFocused(), modal, overGame)) {
            // A click on the game gives the Game the keyboard focus: the keys follow the mouse.
            if (event->type == Fern::EventType::MouseButtonPressed) { m_editorController->focusGameView(); }
            if (m_play.handleEvent(event)) { event->isHandled = true; }
            return;
        }
        m_editorUI.handleEvent(event);
        event->isHandled = true;
        return;
    }

    // The window events: the chrome and the game UI follow the size, quit/close are answered here.
    // They are not marked handled: Panda::Input learns the window size and the focus loss from them.
    m_editorUI.handleEvent(event); // WindowResize syncs the viewport of the chrome
    if (event->type == Fern::EventType::QuitRequest ||
        (event->type == Fern::EventType::WindowCloseRequest && event->getWindow() == m_window)) {
        closeAppRequest();
        event->isHandled = true;
    } else if (event->type == Fern::EventType::WindowResize && event->getWindow() == m_window) {
        windowSizeChanged();
        m_play.windowResized();
    }
}

// Files of the file manager dropped onto the window at a point: a view under it takes them (a
// folder of the Content Browser), else they go into the folder the Content Browser shows. Not
// under a modal, not without a project. EditorMcp drops the same way (`pointer`, action drop).
void EditorLayer::dropFiles(const std::vector<std::string> &paths, PandaUI::Point position) {
    if (!m_loader.hasOpenedProject() || !m_editorController || m_editorUI.context().modals().isPresenting()) { return; }
    if (m_editorUI.context().dropFiles(paths, position)) { return; }
    std::vector<path_t> files;
    for (const std::string &path : paths) { files.push_back(path_t(reinterpret_cast<const char8_t *>(path.c_str()))); }
    m_editorController->importFiles(files);
}

void EditorLayer::windowSizeChanged() {
#ifdef PLATFORM_DESKTOP
    m_window->getDrawingContext()->update(); // the drawable follows the size of the window
#endif
}

/* ------------------------------ The worlds and the game ------------------------------ */

void EditorLayer::play() {
    if (isPlaying()) { return; }
    if (!hasWorld()) {
        LOG_WARN_EDITOR("Nothing to play: no world is open (File \xE2\x86\x92 New \xE2\x86\x92 World, or open a world file).");
        return;
    }
    // A prefab is a part of worlds, not a place a game runs in.
    if (m_documents.activeWorld()->isPrefab()) {
        LOG_WARN_EDITOR(
            "%s is a prefab and is not played: bring the tab of a world to the front.",
            m_documents.activeWorld()->name().c_str()
        );
        return;
    }
    // The game code runs in this process: a crash of it must not take the unsaved edits along.
    m_documents.backUp();
    m_play.begin(editingWorld());
    // Nobody is at the screen (--unattended): the game is driven from its first frame — it has
    // the input whatever has the focus, and a cursor it locks is one of its own, never the
    // cursor of the machine somebody else may be working at.
    if (m_launch.unattended) { m_play.setDriven(true); }
    Editor::selection().setWorld(shownWorld()); // the same ids: the selection moves to the copy
    m_viewports.scene().setWorld(shownWorld());
    // The world tab shows the Game and gives it the focus: the frame and the input of the game
    // are there; the switch of the toolbar shows the Scene of the playing world instead.
    if (m_editorController) {
        m_editorController->openGameView();
        m_editorController->setActiveWorld(panelsWorld()); // panels → the live world
    }
    syncViewports(); // the Game surface has its size before the game starts
    m_play.run();
    LOG_INFO_EDITOR("Entered Play mode");
}

void EditorLayer::stop() {
    if (!isPlaying()) { return; }
    m_play.end();
    Editor::selection().setWorld(shownWorld()); // what the game created or destroyed leaves the selection
    m_viewports.scene().setWorld(shownWorld());
    if (m_editorController) {
        m_editorController->setActiveWorld(panelsWorld()); // panels → the editing world
        m_editorController->showWorldView();                // back to editing: the World view, with its panels
    }
    // Play is a simulation: its asset mutations (script ABI, e.g. terrain digging) do not
    // survive Stop — the affected assets are reloaded from disk. Unsaved EDITOR edits
    // in such an asset (only editor tools set dirty) are rolled back as well.
    if (auto *assetHandler = AssetHandlerEditor::current()) {
        for (AssetId id : GameContext::takeRuntimeMutatedAssets()) {
            if (!assetHandler->isLoaded(id)) { continue; }
            if (assetHandler->documents().isAssetChanged(id)) {
                LOG_WARN_EDITOR(
                    "%s was changed by the game: its unsaved edits are dropped with the changes of the game.",
                    assetHandler->getAssetName(id).c_str()
                );
            }
            assetHandler->reload(id);
        }
    }
    LOG_INFO_EDITOR("Returned to Edit mode");
}

void EditorLayer::scriptsWereBuilt() {
    if (isPlaying()) {
        m_scriptsBuiltWhilePlaying = true;
        LOG_INFO_EDITOR("Scripts built: the editor loads them when the game stops.");
        return;
    }
    m_loader.reloadScriptsDll();
    if (std::exchange(m_playAfterBuild, false)) {
        if (m_editorController) { m_editorController->buildBeforePlayEnded(/*built*/ true); }
        ScriptEngine *scripts = GameContext::getScriptEngine();
        if (scripts != nullptr && scripts->isLoaded()) { play(); }
    }
}

bool EditorLayer::buildBeforePlay(bool ask) {
    if (m_playAfterBuild) { return true; }
    // Another build runs (an export): the game does not wait for what it did not ask.
    if (m_builds.isRunning() || !m_loader.isScriptsBuildBehind()) { return false; }
    m_playAfterBuild = true;
    m_playAfterBuildAsks = ask;
    LOG_INFO_EDITOR("The scripts changed since they were built: the game starts when the build is done.");
    if (m_editorController) { m_editorController->showBuildBeforePlay(); }
    m_builds.start(BuildTargetKind::Scripts, BuildAction::Build);
    return true;
}

// The build Play waited for has ended without new scripts (scriptsWereBuilt plays on success):
// it failed, or it was stopped. The user is asked whether the last build will do — what Visual
// Studio asks; a stop is an answer already.
void EditorLayer::followBuildBeforePlay() {
    if (!m_playAfterBuild || m_builds.isRunning()) { return; }
    m_playAfterBuild = false;
    if (m_editorController) { m_editorController->buildBeforePlayEnded(/*built*/ false); } // the errors stay on screen
    if (!m_playAfterBuildAsks || m_builds.lastResult() != ProjectBuilds::Result::Failed || !m_editorController) { return; }
    ScriptEngine *scripts = GameContext::getScriptEngine();
    if (scripts == nullptr || !scripts->isLoaded()) { return; }
    Editor::showConfirmDialog(
        &m_editorUI.context(), "The scripts did not build",
        "The errors are in the CMake panel. The last build of the scripts can play, without the changes since.",
        "Play Last Build", /*destructive*/ false, [this] { play(); }
    );
}

void EditorLayer::activateWorld(WorldFile &world) {
    if (!isPlaying()) { m_documents.activateWorld(world); }
}

void EditorLayer::closeWorld(WorldFile &world) {
    // The game plays a copy of the active world: it ends with that world.
    if (&world == m_documents.activeWorld()) { stop(); }
    m_documents.closeWorld(world);
}

void EditorLayer::openWorldAsset(AssetId worldAssetId) {
    AssetHandlerEditor *assetHandler = AssetHandlerEditor::current();
    // A world, or a prefab: both are documents of the center (WorldFile).
    const path_t worldPath = assetHandler != nullptr ? assetHandler->documentFilePath(worldAssetId) : path_t();
    if (worldPath.empty()) { return; }
    // Nothing is dropped: the world opens in a tab of its own next to the open ones. The game
    // plays a copy of the active world, so it stops before another world takes that place.
    stop();
    if (m_documents.openWorld(worldPath) == nullptr) {
        LOG_ERROR_EDITOR("Failed to open world: %s", worldPath.string().c_str());
        return;
    }
    // Its tab comes forward, also when the world was open and active already (behind another tab).
    if (m_editorController) { m_editorController->showWorldView(); }
}

void EditorLayer::newWorld() {
    // A new world opens in a tab of its own next to the open ones: nothing is dropped.
    stop();
    m_documents.newWorld();
}

// What the Hierarchy says it shows: the name of the document in front, and of a prefab its one
// tree.
void EditorLayer::showDocumentName() {
    if (!m_editorController) { return; }
    WorldFile *active = m_documents.activeWorld();
    Entity prefabRoot = active != nullptr ? active->prefabRoot() : Entity();
    m_editorController->setWorldDisplayName(worldDisplayName(), prefabRoot.isValid() ? prefabRoot.getId() : UUID(0));
}

std::string EditorLayer::worldDisplayName() {
    if (!hasWorld()) { return {}; }
    const path_t &worldPath = m_documents.activeWorld()->path();
    return worldPath.empty() ? "Untitled World" : worldPath.filename().stem().string();
}

void EditorLayer::documentsWillActivateWorld() {
    // The game plays a copy of the world in front: it ends before that world leaves the front
    // (the tabs do not switch while it runs; opening a world stops it). Then what the tab of the
    // world shows of it — its tree, its selection — stays with the tab.
    stop();
    if (m_editorController) { m_editorController->worldWillDeactivate(); }
}

void EditorLayer::documentsDidActivateWorld() {
    // The game plays a copy of the world that was active when it started: it ends before another
    // world takes that place (the tabs do not switch while it runs; opening a world stops it).
    stop();
    m_viewports.scene().setWorld(shownWorld());
    Editor::selection().deselectEntities(); // the selection is of one world, and ids repeat between worlds
    Editor::selection().setWorld(shownWorld());
    if (m_editorController) {
        m_editorController->worldContextChanged(); // the history of the selection points into the previous world
        m_editorController->setActiveWorld(panelsWorld());
    }
    worldDidChange();
    // The tab of this world shows what it showed when it was left.
    if (m_editorController) { m_editorController->worldDidActivate(); }
}

void EditorLayer::documentsDidLoadWorld() {
    worldDidChange();
}

void EditorLayer::worldDidChange() {
    Editor::selection().deselectEntities();
    m_menuBar.rebuild();
    if (!m_editorController) { return; }
    m_editorController->worldContextChanged();
    showDocumentName();
    m_editorController->refreshFromWorld(); // the Hierarchy rereads the tree
    m_editorController->worldTabs().sync(); // no tab outlives its world
}

EditorCameraSettings EditorLayer::documentsEditorCamera() {
    EditorCameraSettings settings;
    settings.position = m_viewports.cameraController().getPosition();
    settings.rotation = m_viewports.cameraController().getRotation();
    return settings;
}

// A world shown for the first time has no place for the editor camera yet: the editor looks at
// it the way the game will, from its main camera. A world without one leaves the camera where
// it is.
void EditorLayer::documentsLookAtWorld() {
    World &world = editingWorld();
    // A prefab has no camera of its own to look from: the editor camera frames its entities.
    if (isEditingPrefab()) {
        Entity root = m_documents.activeWorld()->prefabRoot();
        glm::vec3 low(0.f), high(0.f);
        bool any = false;
        for (Entity entity : world.subtree(root)) {
            const glm::vec3 position = glm::vec3(world.getWorldSpaceTransformMatrix(entity)[3]);
            low = any ? glm::min(low, position) : position;
            high = any ? glm::max(high, position) : position;
            any = true;
        }
        if (any) {
            const float radius = std::max(glm::length(high - low) * 0.5f + 1.f, 1.f);
            m_viewports.cameraController().frame((low + high) * 0.5f, radius, m_viewports.editorCamera().getFieldOfView());
        }
        return;
    }
    Entity camera = world.findMainCameraEntity();
    if (!camera.isValid()) { return; }
    const glm::mat4 transform = world.getWorldSpaceTransformMatrix(camera);
    glm::mat3 axes(transform);
    for (int i = 0; i < 3; ++i) { axes[i] = glm::normalize(axes[i]); }
    m_viewports.cameraController().setPosition(glm::vec3(transform[3]));
    m_viewports.cameraController().setRotation(glm::quat_cast(axes));
}

void EditorLayer::documentsSetEditorCamera(const EditorCameraSettings &camera) {
    EditorCameraSettings pose = camera; // the camera controller takes the glm types of a mutable pose
    m_viewports.cameraController().setPosition(pose.position);
    m_viewports.cameraController().setRotation(pose.rotation);
}

/* ------------------------------ The Scene viewport ------------------------------ */

void EditorLayer::setGizmoOperation(int mode) {
    const Gizmos::Operation op = mode == 1   ? Gizmos::Operation::Rotate
                                 : mode == 2 ? Gizmos::Operation::Scale
                                             : Gizmos::Operation::Translate;
    m_viewports.gizmo().setOperation(op);
}

void EditorLayer::setGizmoSpace(bool worldSpace) {
    m_viewports.gizmo().setSpace(worldSpace ? Gizmos::Space::World : Gizmos::Space::Local);
}

void EditorLayer::rectSelect(
    PandaUI::Point start, PandaUI::Point current, bool additive, Editor::RectSelectPhase phase
) {
    if (phase == Editor::RectSelectPhase::Drop) {
        m_viewports.scene().dropRectSelection();
        return;
    }
    m_viewports.scene().driveRectSelection(
        IVec2(static_cast<int>(start.x), static_cast<int>(start.y)),
        IVec2(static_cast<int>(current.x), static_cast<int>(current.y)),
        additive,
        phase == Editor::RectSelectPhase::Commit
    );
}

void EditorLayer::resetCamera() {
    m_viewports.cameraController().frame(glm::vec3(0.f), 1.5f, m_viewports.editorCamera().getFieldOfView());
}

// The camera flies to where the selection fits the frame: the box around the meshes of the
// selected entities and of everything under them (a model is selected by its root), or a meter
// around an entity that draws nothing (a light, an empty).
void EditorLayer::focusSelection() {
    if (!Editor::selection().hasEntities()) { return; }
    World &world = *shownWorld();
    std::vector<UUID> ids;
    std::vector<UUID> pending;
    for (UUID id : Editor::selection().entityIds()) { pending.push_back(id); }
    while (!pending.empty()) {
        const UUID id = pending.back();
        pending.pop_back();
        Entity entity = world.getById(id);
        if (!entity.isValid()) { continue; }
        ids.push_back(id);
        for (UUID child : entity.getChildEntities()) { pending.push_back(child); }
    }
    glm::vec3 center = Editor::selection().medianPosition();
    float radius = 1.f;
    glm::vec3 boundsMin, boundsMax;
    if (m_viewports.worldView().meshBounds(boundsMin, boundsMax, &ids)) {
        center = (boundsMin + boundsMax) * 0.5f;
        radius = std::max(glm::length(boundsMax - boundsMin) * 0.5f, 0.1f);
    }
    m_viewports.cameraController().frame(center, radius, m_viewports.editorCamera().getFieldOfView());
}

/* ------------------------------ The settings ------------------------------ */

TypeContext &EditorLayer::settingsStore(SettingsScope scope) {
    return scope == SettingsScope::Project ? m_documents.projectSettingsFile().settings()
                                           : m_documents.editorSettingsFile().settings();
}

std::filesystem::path EditorLayer::settingsFilePath(Panda::SettingsScope scope) {
    return scope == SettingsScope::Project ? m_documents.projectSettingsFile().path() : m_documents.editorSettingsFile().path();
}

void EditorLayer::applyEditorSettings() {
    m_viewportSettings = m_documents.editorSettings().viewport;
    m_viewports.cameraController().setMoveSpeed(m_viewportSettings.cameraSpeed);
    m_updater.setAutoUpdate(m_documents.editorSettings().updates.autoUpdate);
    m_pluginStore.setAutoUpdate(m_documents.editorSettings().updates.autoUpdate);
    m_editorUI.applyFonts(m_appearance.fonts()); // a changed font or size: the atlases reload and the texts re-measure
}

void EditorLayer::editorSettingsChanged() {
    // The editor scope applies at once and is written on every change.
    applyEditorSettings();
    m_documents.editorSettingsFile().save();
}

void EditorLayer::documentsDidReadSettings(SettingsScope scope) {
    // The file was reread (Reload, Don't Save, a revert): the scope applies, and the settings tab
    // rereads its store.
    if (scope == SettingsScope::Editor) { applyEditorSettings(); } // applied, not written back
    if (scope == SettingsScope::Project) { m_loader.applyProjectSettings(); }
    if (m_editorController) { m_editorController->settingsFileReloaded(scope); }
}

void EditorLayer::documentsDidWriteSettings(SettingsScope scope) {
    // The editor scope applies before it is written; a saved project scope brings its shader features.
    if (scope == SettingsScope::Project) { m_loader.applyProjectSettings(); }
}

/* ------------------------------ The project and the window ------------------------------ */

void EditorLayer::loaderDidLoadProject(const std::string &name, const path_t &path) {
    m_backupsAsked = false; // what its last session left behind is offered once the project shows
    // Game saves, when launched from the editor, live next to the project (the folder is made
    // when a script asks for it).
    GameContext::setPersistentDataPath(path / ".Panda" / "SaveData");
    updateWindowState();
    m_menuBar.rebuild();
    // Asset panels rescan the new project (at startup the controller does not exist yet —
    // then setWorld reads the registry right after creation).
    if (m_editorController) { m_editorController->refreshAssetPanels(); }
    loadEditorParts(); // at startup the controller does not exist yet — then after its creation (onAttach)
    showEditorChrome();
    m_diskWatch.watch(path); // from here on its files are followed
}

// The open project goes — Close Project, another project in this window, the same one read
// again: what the editor holds of its worlds and its assets is let go here, in one place, while
// they are still there. The ids of assets repeat between projects (two projects made from one
// template), so nothing that names an asset by id may outlive its project.
void EditorLayer::loaderWillCloseProject() {
    m_diskWatch.watch({});
    m_play.world().clear();
    Editor::selection().clear();
    if (m_editorController) {
        m_editorController->closeAuxiliaryWindow(); // what it shows is the project's
        m_editorController->projectWillClose();    // the tabs of its assets, the history
    }
    if (m_previews) { m_previews->clear(); } // the previews and the thumbnails hold its assets
}

// The inspector shows the scripts of the build that was loaded: a field published since, a class
// that is gone.
void EditorLayer::loaderDidReloadScripts() {
    if (m_editorController) { m_editorController->refreshInspector(); }
}

void EditorLayer::loaderDidLoadCloseProject() {
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

void EditorLayer::loadEditorParts() {
    if (!m_loader.hasOpenedProject() || !m_editorController) { return; }
    const size_t iconFolders = Editor::editorExtensions().iconFolders.size();
    if (ProjectModules::loadEditorParts(m_documents.projectSettings().disabledModules) > 0) {
        // The editor parts register their editor-scope sections after settings.json was read: the
        // sections kept as foreign values get their types now.
        SettingsMapper::adoptForeign(SettingsScope::Editor, m_documents.editorSettingsFile().settings());
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

// What the disk watch waits for besides the editor in front: no button of the mouse held in a
// window of the editor.
void EditorLayer::followDiskWatchEvent(const Fern::Event &event) {
    switch (event.type) {
        case Fern::EventType::MouseButtonPressed: m_buttonsHeld++; break;
        case Fern::EventType::MouseButtonReleased: m_buttonsHeld = std::max(0, m_buttonsHeld - 1); break;
        case Fern::EventType::WindowLostFocus: m_buttonsHeld = 0; break; // the release goes to another application
        default: break;
    }
    m_diskWatch.pointerHeld(m_buttonsHeld > 0);
}

bool EditorLayer::isModalOpen() {
    return m_editorUI.context().modals().isPresenting();
}

bool EditorLayer::isReopenNeededForModules() {
    return m_loader.hasOpenedProject() && ProjectModules::loadNeeded(m_documents.projectSettings().disabledModules);
}

bool EditorLayer::reopenProjectForModules() {
    if (ProjectModules::loadNeeded(m_documents.projectSettings().disabledModules)) {
        // A module switched on in the file: its contributions are needed before the asset registry
        // and the world, so the project opens again — that reads everything fresh.
        LOG_INFO_EDITOR("project.json switches a module on: the project is opened again");
        const path_t projectPath = m_loader.getOpenedProjectPath();
        m_documentActions.runAfterUnsavedQuestion([this, projectPath]() {
            saveOpenedProjectWindowState();
            m_loader.openProject(projectPath);
        });
        return true;
    }
    warnOfModulesSwitchedOff();
    return false;
}

void EditorLayer::warnOfModulesSwitchedOff() {
    if (!m_loader.hasOpenedProject()) { return; }
    if (ProjectModules::restartNeeded(m_documents.projectSettings().disabledModules)) {
        LOG_WARN_EDITOR("A module was switched off in project.json: restart the editor to drop it.");
    }
}

void EditorLayer::openProjectRequest() {
    // The folder first: the question about unsaved edits is for a project that will open.
    std::optional<path_t> folder = SystemTools::openFolderDialog();
    if (folder.has_value()) { openProject(*folder); }
}

void EditorLayer::openProject(const path_t &path) {
    if (path == m_loader.getOpenedProjectPath()) { return; }
    // A folder that is not a project is said so before the question: the open project stays as
    // it is, with its edits.
    std::string problem;
    if (ProjectLoader::readName(path, &problem).empty()) {
        LOG_ERROR_EDITOR("%s", problem.c_str());
        return;
    }
    m_documentActions.runAfterUnsavedQuestion([this, path]() {
        saveOpenedProjectWindowState();
        m_loader.openProject(path);
    });
}

void EditorLayer::closeProjectRequest() {
    m_documentActions.runAfterUnsavedQuestion([this]() { closeProject(); });
}

void EditorLayer::closeProject() {
    saveOpenedProjectWindowState();
    m_loader.closeProject(); // loaderWillCloseProject lets go of what names the project
}

void EditorLayer::closeAppRequest() {
    m_documentActions.runAfterUnsavedQuestion([this]() { closeApp(); });
}

void EditorLayer::closeApp() {
    saveOpenedProjectWindowState();
    Application::get()->close();
}

void EditorLayer::restartEditor() {
    // The same flow as Quit, the unsaved-changes dialog included; main() starts the new instance
    // once this one has written its settings and torn the window down.
    m_documentActions.runAfterUnsavedQuestion([this]() {
        // The new instance opens the project of this one: the session file is shared by every
        // running editor, the command line is not.
        std::vector<std::string> arguments;
        if (m_loader.hasOpenedProject()) {
            arguments = {SystemTools::PROJECT_ARGUMENT, m_loader.getOpenedProjectPath().string()};
        }
        if (m_launch.mcpPort > 0) { // the agent that restarted the editor finds it on the same port
            arguments.push_back(SystemTools::MCP_ARGUMENT);
            arguments.push_back(std::to_string(m_launch.mcpPort));
        }
        if (SystemTools::isUserDataDirectoryOverridden()) { // the new instance keeps the settings of this one
            arguments.push_back(SystemTools::USER_DATA_ARGUMENT);
            arguments.push_back(SystemTools::userDataDirectory().string());
        }
        if (m_launch.unattended) { arguments.push_back(SystemTools::UNATTENDED_ARGUMENT); }
        SystemTools::setRelaunchOnExit(true, std::move(arguments));
        closeApp();
    });
}

void EditorLayer::newWindow() {
    // The session is written first: the new instance reads the recent projects from it.
    m_loader.saveSession();
    if (!SystemTools::startNewInstance({SystemTools::NEW_WINDOW_ARGUMENT})) {
        LOG_ERROR_EDITOR("A new editor window could not be started");
    }
}

void EditorLayer::showStartScreen() {
    if (!m_startScreenController) { return; }
    m_startScreenController->refreshRecentProjects();
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

void EditorLayer::updateWindowState() {
    if (m_loader.hasOpenedProject()) {
        m_window->setFlags(FERN_WINDOW_FLAGS_DEFAULT);
        auto windowState = m_loader.getLastWindowState();
        if (m_launch.unattended) {
            // One size whatever the display and the last session: a UI test meets the same layout.
            m_window->setState(Fern::WindowState::WindowStateNormal);
            m_window->setSize({UNATTENDED_WINDOW_WIDTH, UNATTENDED_WINDOW_HEIGHT});
        } else if (windowState.isFullScreen) {
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
        m_window->setState(Fern::WindowState::WindowStateNormal);
        m_window->setSize({800, 600});
    }
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

// Several editors run at once, one project each: the window title and the label on the
// application icon say which project this one holds. Followed every frame — the world file
// changes on Open, New and Save As, and a comparison of two strings costs nothing.
void EditorLayer::syncWindowIdentity(double deltaTime) {
    // Unsaved: looked at four times a second — the documents are many, and the dot may come a
    // moment after the edit.
    m_unsavedCheck -= deltaTime;
    if (m_unsavedCheck <= 0.0) {
        m_unsavedCheck = 0.25;
        const bool unsaved = m_loader.hasOpenedProject() && m_documents.hasUnsaved();
        if (unsaved != m_window->getModifiedState()) { m_window->setModifiedState(unsaved); }
    }
    std::string project;
    std::string title = "Welcome";
    if (m_loader.hasOpenedProject()) {
        project = m_loader.projectName();
        // The project and the world in front; the project alone while no world is open.
        title = hasWorld() ? project + " \xE2\x80\x94 " + m_documents.activeWorld()->name() : project; // an em dash, UTF-8
    }
    if (title != m_windowTitle) {
        m_windowTitle = title;
        m_window->setTitle(title.c_str());
    }
    if (project != m_iconLabel) {
        m_iconLabel = project;
        Fern::setApplicationIconLabel(project.c_str());
    }
}

bool EditorLayer::openAuxiliaryWindow(const char *title, PandaUI::Size size, std::shared_ptr<PandaUI::View> root) {
    return m_editorUI.openAuxiliaryWindow(title, size, std::move(root)) != nullptr;
}

void EditorLayer::closeAuxiliaryWindow() {
    m_editorUI.closeAuxiliaryWindow();
}

/* ------------------------------ The commands and the menu bar ------------------------------ */

// The commands of the application and of the project: what the menu bar shows and the palette
// lists. Their keys are the keymap's (UI/DefaultKeymap.cpp). A command of a project answers
// false without one.
void EditorLayer::registerCommands() {
    auto command = [this](const char *id, const char *group, const char *label, std::function<void()> run, bool needsProject = true) {
        m_commands.add(id, group, label, [this, run = std::move(run), needsProject] {
            if (needsProject && !m_loader.hasOpenedProject()) { return false; }
            run();
            return true;
        });
    };
    const bool anywhere = false;
    command("file.newWorld", "File", "New World", [this] { newWorld(); });
    command("file.newWindow", "File", "New Window", [this] { newWindow(); }, anywhere);
    command("file.openProject", "File", "Open Project", [this] { openProjectRequest(); }, anywhere);
    command("file.saveAll", "File", "Save All", [this] { m_documentActions.saveAll(); });
    command("file.showCppProject", "File", "Show C++ Project", [this] { m_builds.showFolder(BuildTargetKind::Scripts); });
    command("file.closeProject", "File", "Close Project", [this] { closeProjectRequest(); });
    command("app.exit", "File", "Exit", [this] { closeAppRequest(); }, anywhere);
    // The scripts in the configuration the CMake panel shows; the panel has the Export project too.
    const auto scripts = [this](BuildAction action) {
        m_editorController->showPanel("cmake");
        m_builds.start(BuildTargetKind::Scripts, action);
    };
    command("build.scripts", "Build", "Build Scripts", [scripts] { scripts(BuildAction::Build); });
    command("build.configure", "Build", "Configure Scripts", [scripts] { scripts(BuildAction::Configure); });
    command("build.clean", "Build", "Clean Scripts Build Folder", [scripts] { scripts(BuildAction::Clean); });
    command("build.stop", "Build", "Stop Build", [this] { m_builds.cancel(); });
    // The export of the game is a window of its own: the target, the steps, what came of it.
    command("export.open", "File", "Export Game", [this] { m_editorController->showExportWindow(); });
    command("settings.openFile", "Settings", "Open Settings File", [this] { showSettingsFile(SettingsScope::Editor); }, anywhere);
    command("settings.openProjectFile", "Settings", "Open Project Settings File", [this] {
        showSettingsFile(SettingsScope::Project);
    });
    command("keymap.openFile", "Settings", "Open Keymap File", [this] { showKeymapFile(); }, anywhere);
    command("help.checkForUpdates", "Help", "Check for Updates", [this] { checkForUpdates(true); }, anywhere);
    m_commands.add("help.restartToUpdate", "Help", "Restart to Update", [this] {
        if (!m_updater.hasReadyUpdate()) { return false; }
        restartEditor();
        return true;
    });
    command("help.releaseNotes", "Help", "Release Notes", [this] { SystemTools::openUrl(m_updater.releaseNotesUrl()); }, anywhere);
    command("help.about", "Help", "About", [this] { showAbout(); }, anywhere);
}

// "Open … File": the JSON of the scope in the external editor. A file that was never written (a
// fresh install, a project made outside the editor) is written first with the values the editor
// has, so there is something to open.
void EditorLayer::showSettingsFile(SettingsScope scope) {
    SettingsFile &file =
        scope == SettingsScope::Project ? m_documents.projectSettingsFile() : m_documents.editorSettingsFile();
    const path_t path = file.path();
    if (path.empty()) { // the project scope: no project is open
        if (scope == SettingsScope::Editor) {
            LOG_ERROR_EDITOR("No user data directory: the editor settings are not persisted");
        }
        return;
    }
    if (!std::filesystem::exists(path) && !file.save()) { return; }
    openInExternalEditor(path);
}

// The keymap file in the external editor. A file that was never written gets an empty object, so
// there is something to open; what a row holds is in PandaSDK/docs/editor.md.
void EditorLayer::showKeymapFile() {
    const path_t &path = m_commands.keymapPath();
    if (path.empty()) {
        LOG_ERROR_EDITOR("No user data directory: the keymap is not persisted");
        return;
    }
    std::string error;
    if (!std::filesystem::exists(path) && !Fern::writeFileAtomically(path, "{\n}\n", &error)) {
        LOG_ERROR_EDITOR("The keymap is not made: %s.", error.c_str());
        return;
    }
    openInExternalEditor(path);
}

void EditorLayer::showAbout() {
    Editor::AboutInfo info;
    info.version = EditorUpdater::versionString();
    info.channel = ENGINE_RELEASE_CHANNEL;
    info.build = ENGINE_BUILD_NUMBER;
    info.commit = ENGINE_BUILD_COMMIT;
    info.platform = EditorUpdater::platformString();
    info.updatesEnabled = EditorUpdater::isEnabled();
    if (m_updater.state().kind != UpdateState::Kind::Idle) { info.updateStatus = m_updater.statusLine(); }
    info.onCheckForUpdates = [this] { checkForUpdates(true); };
    info.onReleaseNotes = [this] { SystemTools::openUrl(m_updater.releaseNotesUrl()); };
    Editor::showAboutDialog(&m_editorUI.context(), info);
}

void EditorLayer::openInExternalEditor(const std::filesystem::path &path, int line, int column) {
    if (path.empty()) { return; }
    const std::string &command = m_documents.editorSettings().externalTools.editor;
    // The text form of the file: a text document opens as it is; a binary asset (a texture, a
    // model) is described by its sidecar. A shader is a pair of files: both open.
    static const char *TEXT_EXTENSIONS[] = {".world", ".mat",  ".json", ".hlsl", ".hlsli", ".txt",
                                            ".md",    ".cpp",  ".hpp",  ".h",    ".c",     ".cmake",
                                            ".gltf",  ".pmeta"};
    const std::string extension = path.extension().string();
    bool text = false;
    for (const char *known : TEXT_EXTENSIONS) {
        if (extension == known) { text = true; }
    }
    path_t target = path;
    if (!text) {
        path_t sidecar = path;
        sidecar += ".pmeta";
        if (std::filesystem::exists(sidecar)) { target = sidecar; }
    }
    // A place is of the file that was named: not of the sidecar that stands in for a binary one.
    const bool placed = target == path;
    SystemTools::openInEditor(command, target, placed ? line : 0, placed ? column : 0);
    if (line > 0) { return; } // a place of one file: its pair is not opened next to it
    const std::string name = path.filename().string();
    const std::string vertexSuffix = "vert.hlsl";
    if (name.size() > vertexSuffix.size() && name.compare(name.size() - vertexSuffix.size(), vertexSuffix.size(), vertexSuffix) == 0) {
        path_t fragment = path;
        fragment.replace_filename(name.substr(0, name.size() - vertexSuffix.size()) + "frag.hlsl");
        if (std::filesystem::exists(fragment)) { SystemTools::openInEditor(command, fragment); }
    }
}

void EditorLayer::revealInFileManager(const std::filesystem::path &path) {
    if (!path.empty()) { SystemTools::revealInFileManager(path); }
}

bool EditorLayer::menuBarHasOpenedProject() {
    return m_loader.hasOpenedProject();
}

void EditorLayer::menuBarOpenProject(const RecentProject &project) {
    openProject(project.path);
}

std::vector<std::string> EditorLayer::menuBarNewEntityTitles() {
    return m_editorController ? titlesOf(m_editorController->newEntityActions()) : std::vector<std::string>();
}

std::vector<std::string> EditorLayer::menuBarNewAssetTitles() {
    return m_editorController ? titlesOf(m_editorController->newAssetActions()) : std::vector<std::string>();
}

void EditorLayer::menuBarNewEntity(size_t index) {
    if (m_editorController) { runAction(m_editorController->newEntityActions(), index); }
}

void EditorLayer::menuBarNewAsset(size_t index) {
    if (m_editorController) { runAction(m_editorController->newAssetActions(), index); }
}

const std::vector<RecentProject> &EditorLayer::menuBarGetRecentProjectsList() {
    return m_loader.getRecentProjectsList();
}

const path_t &EditorLayer::menuBarGetOpenedProjectPath() {
    return m_loader.getOpenedProjectPath();
}

bool EditorLayer::menuBarUpdateReady() {
    return m_updater.hasReadyUpdate();
}

/* ------------------------------ The editor updater ------------------------------ */

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

void EditorLayer::updateIndicatorDidActivate() {
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

void EditorLayer::updateIndicatorDidDismiss() {
    m_updater.dismiss();
}

void EditorLayer::pushUpdateStatus() {
    const UpdateState &state = m_updater.state();
    const bool dismissed = m_updater.isDismissed();
    if (state.revision == m_shownUpdateRevision && dismissed == m_shownUpdateDismissed) { return; }
    m_shownUpdateRevision = state.revision;
    m_shownUpdateDismissed = dismissed;
    const Editor::UpdateIndicatorModel model = Editor::updateIndicatorModel(m_updater);
    if (m_editorController) { m_editorController->setUpdateStatus(model); }
    if (m_startScreenController) { m_startScreenController->setUpdateStatus(model); }
    const bool ready = m_updater.hasReadyUpdate();
    if (ready != m_shownUpdateReady) {
        m_shownUpdateReady = ready;
        m_menuBar.rebuild(); // "Restart to Update" appears and goes with the installed update
    }
}

} // namespace Panda
