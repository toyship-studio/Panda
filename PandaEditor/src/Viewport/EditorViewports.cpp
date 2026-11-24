#include "Viewport/EditorViewports.hpp"

#include "Viewport/EditorWorldPasses.hpp"

#include <Panda/Assets/PostprocessingAsset.hpp>
#include <Panda/GameLogic/Components/BaseComponents.hpp>
#include <Panda/GameLogic/Input.hpp>

namespace Panda {

namespace {
    const Size INITIAL_SIZE{100.f, 100.f}; // before the panels have a layout
}

EditorViewports::EditorViewports()
    : m_passTargets()
    , m_composite()
    , m_sceneSurface()
    , m_gameSurface()
    , m_editorCamera()
    , m_cameraController()
    , m_scene(&m_cameraController, &m_passTargets)
    , m_grid(&m_cameraController)
    , m_helpers() {}

void EditorViewports::init() {
    m_passTargets.init(INITIAL_SIZE);
    m_composite.init();
    m_sceneSurface.init(INITIAL_SIZE);
    m_gameSurface.init(INITIAL_SIZE);
    m_scene.setCamera(&m_editorCamera);
}

void EditorViewports::sync(const ViewportFrames &frames) {
    // The viewport the world tab shows is the one that renders.
    m_live = frames.shown;
    m_onScreen = frames.visible;
    m_sceneSurface.setFrame(frames.scene);
    m_scene.setFrame(frames.scene);
    m_gameSurface.setFrame(frames.game);
    const ViewportSurface &live = liveSurface();
    if (live.isReady()) {
        m_view.resize(live.pixelWidth(), live.pixelHeight());
        m_passTargets.resize(live.frame().size);
    }
    // Panda::Input is the game's: its viewport is the Game surface, whichever viewport renders.
    if (m_gameSurface.isReady()) { Input::setViewportFrame(m_gameSurface.frame()); }
}

void EditorViewports::beginFrame(
    World &world, double deltaTime, const EditorViewportSettings &settings,
    const CameraController::Navigation &navigation
) {
    // The debug wireframe is a display option of the Scene; the Game shows the game.
    m_view.renderer3D().setForceWireframe(settings.wireframe && isSceneLive());
    Camera *mainCamera = isGameLive() ? world.findMainCamera() : nullptr;
    m_gameHasCamera = !isGameLive() || mainCamera != nullptr;
    m_sceneCameraFrame = mainCamera == nullptr;
    m_mainCameraFrame = mainCamera != nullptr;
    if (mainCamera != nullptr) {
        // The game camera: its matrices are asked after the simulation (the scripts move it in
        // the same frame); its aspect is the Game surface's.
        mainCamera->setViewportSize(m_gameSurface.frame().size);
        return;
    }
    // The editor camera: the Scene, or the Game of a world without a main camera.
    m_cameraController.update(static_cast<float>(deltaTime), navigation);
    const ViewportSurface &surface = liveSurface();
    if (surface.isReady()) { m_editorCamera.setViewportSize(surface.frame().size); }
    const glm::mat4 projection = m_editorCamera.getProjection();
    m_sceneView = m_cameraController.getViewMatrix();
    m_sceneViewProj = projection * m_sceneView;
    m_camera = ViewCamera{};
    m_camera.projection = projection;
    m_camera.viewProj = m_sceneViewProj;
    m_camera.skyViewProj = projection * m_cameraController.getSkyViewMatrix();
    m_camera.position = m_cameraController.getPosition();
}

void EditorViewports::render(World &world, const EditorViewportSettings &settings, float time) {
    // A world whose main camera went away during the simulation keeps the camera of its last frame.
    if (m_mainCameraFrame) { ViewCamera::main(world, m_camera); }
    // The passes of the frame in the order they are executed: the world (its shadows, then the
    // scene), the editor passes of the Scene, the look of the frame, the composite into the output.
    m_view.render(world, m_camera);
    const ViewportSurface &surface = liveSurface();
    if (!surface.isReady()) { return; }
    if (isSceneLive()) { renderScenePasses(world, settings); }
    const Miren::TextureHandle color = develop(world, settings, time);
    const bool outlined = isSceneLive(); // the selection is shown in the Scene, never in the game
    m_composite.draw(
        surface.addView(), color, outlined ? m_passTargets.selectionMask() : m_composite.emptyMask(),
        outlined ? m_passTargets.outlineStep() : glm::vec4(0.f)
    );
}

void EditorViewports::renderScenePasses(World &world, const EditorViewportSettings &settings) {
    EditorWorldPasses::renderSelectionOverlay(m_view, world, m_passTargets.addSelectionView());
    const Miren::ViewId idsView = m_passTargets.addIdsView();
    EditorWorldPasses::renderPicking(m_view, world, idsView);
    // What the editor draws into the scene over the world goes into the scene pass itself.
    if (settings.showGrid) { m_grid.update(m_sceneViewProj, m_view.sceneView()); }
    if (settings.showHelperIcons) {
        m_helpers.render(&world, m_sceneViewProj, m_editorCamera.getViewportSize(), m_view.sceneView(), idsView);
    }
}

Miren::TextureHandle EditorViewports::develop(World &world, const EditorViewportSettings &settings, float time) {
    // HDR: the scene is linear 16F, the pipeline ALWAYS runs (at minimum an implicit Tonemap). The
    // display option of the Scene and camera.postprocessingEnabled control only the user chain;
    // the Game always shows the look of the game.
    const bool showChain = isGameLive() || settings.showPostprocessing;
    AssetRef<PostprocessingAsset> chain;
    PostprocessingFrameInputs inputs = m_view.postprocessingInputs(world);
    inputs.cameraNear = m_editorCamera.getNear();
    inputs.cameraFar = m_editorCamera.getFar();
    Entity cameraEntity = world.findMainCameraEntity();
    if (cameraEntity.isValid()) {
        CameraComponent &camera = cameraEntity.getComponent<CameraComponent>();
        if (showChain && camera.postprocessingEnabled) { chain = camera.postprocessing.as<PostprocessingAsset>(); }
        inputs.setCamera(camera);
        inputs.bloom.enabled = showChain && camera.bloomEnabled;
        inputs.exposure.enabled = showChain && camera.autoExposureEnabled;
        if (m_sceneCameraFrame) { // the frame is the editor camera's: its clip planes
            inputs.cameraNear = m_editorCamera.getNear();
            inputs.cameraFar = m_editorCamera.getFar();
        }
    }
    return m_view.develop(chain, inputs, time);
}

} // namespace Panda
