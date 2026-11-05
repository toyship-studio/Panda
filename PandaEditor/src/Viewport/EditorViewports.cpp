#include "Viewport/EditorViewports.hpp"

#include "Viewport/EditorWorldPasses.hpp"

#include <Fern/Window/Window.hpp>
#include <Panda/Application/Application.hpp>
#include <Panda/Assets/PostprocessingAsset.hpp>
#include <Panda/GameLogic/Components/BaseComponents.hpp>
#include <Panda/GameLogic/Input.hpp>
#include <Panda/Renderer/MirenViewDistribution.hpp>

namespace Panda {

namespace {
    const Size INITIAL_SIZE{100.f, 100.f}; // before the panels have a layout
}

EditorViewports::EditorViewports()
    : m_targets()
    , m_composite()
    , m_sceneSurface()
    , m_gameSurface()
    , m_editorCamera()
    , m_cameraController()
    , m_scene(&m_cameraController, &m_targets)
    , m_grid(&m_cameraController)
    , m_helpers() {}

void EditorViewports::init() {
    m_targets.init(INITIAL_SIZE);
    m_composite.init();
    m_sceneSurface.init(INITIAL_SIZE);
    m_gameSurface.init(INITIAL_SIZE);
    m_scene.setCamera(&m_editorCamera);
}

void EditorViewports::sync(const ViewportFrames &frames, bool playing) {
    if (playing) {
        m_live = frames.gameVisible || !frames.sceneVisible ? ViewportKind::Game : ViewportKind::Scene;
    } else {
        m_live = frames.sceneVisible || !frames.gameVisible ? ViewportKind::Scene : ViewportKind::Game;
    }
    m_sceneSurface.setFrame(frames.scene);
    m_scene.setFrame(frames.scene);
    m_gameSurface.setFrame(frames.game);
    const ViewportSurface &live = liveSurface();
    if (live.isReady()) {
        m_targets.resize(live.frame().size);
        Input::setViewportFrame(live.frame()); // the mouse of the game and of the editor is relative to the live viewport
    }
}

WorldFrame EditorViewports::beginFrame(
    World &world, double deltaTime, bool simulate, const EditorViewportSettings &settings
) {
    // The debug wireframe is a display option of the Scene; the Game shows the game.
    world.getRenderer3D().setForceWireframe(settings.wireframe && isSceneLive());
    m_frame = WorldFrame{};
    m_frame.simulate = simulate;
    Camera *mainCamera = isGameLive() ? world.findMainCamera() : nullptr;
    m_gameHasCamera = !isGameLive() || mainCamera != nullptr;
    m_sceneCameraFrame = mainCamera == nullptr;
    if (mainCamera != nullptr) {
        // The game camera: its projection is resolved by World::update after the simulation (the
        // scripts move it in the same frame); its aspect is the Game surface's.
        mainCamera->setViewportSize(m_gameSurface.frame().size);
        m_frame.mainCamera = true;
        return m_frame;
    }
    // The editor camera: the Scene, or the Game of a world without a main camera.
    m_cameraController.update(deltaTime);
    const ViewportSurface &surface = liveSurface();
    if (surface.isReady()) { m_editorCamera.setViewportSize(surface.frame().size); }
    const glm::mat4 projection = m_editorCamera.getProjection();
    m_sceneView = m_cameraController.getViewMatrix();
    m_sceneViewProj = projection * m_sceneView;
    m_frame.viewProj = m_sceneViewProj;
    m_frame.skyViewProj = projection * m_cameraController.getSkyViewMatrix();
    m_frame.cameraPosition = m_cameraController.getPosition();
    return m_frame;
}

void EditorViewports::finishFrame(World &world, const EditorViewportSettings &settings, float time) {
    // The game UI draws into the Game surface whatever is live: in a Scene-live frame it lands on
    // the last game frame, which the panel marks as not rendering.
    m_gameSurface.bindView(Views::GAME_UI_VIEW);
    const ViewportSurface &surface = liveSurface();
    if (!surface.isReady()) { return; }
    if (isSceneLive()) { renderScenePasses(world, settings); }
    const Miren::TextureHandle color = runPostprocessing(world, settings, time);
    surface.bindView(Views::EDITOR_VIEWPORT_VIEW);
    m_composite.draw(color, isSceneLive() ? m_targets.selectionMask() : m_composite.emptyMask());
}

void EditorViewports::renderScenePasses(World &world, const EditorViewportSettings &settings) {
    EditorWorldPasses::renderSelectionOverlay(world, m_sceneViewProj);
    EditorWorldPasses::renderPicking(world, m_frame);
    m_grid.update(m_sceneViewProj);
    if (settings.showHelperIcons) { m_helpers.render(&world, m_sceneViewProj, m_editorCamera.getViewportSize()); }
}

Miren::TextureHandle EditorViewports::runPostprocessing(
    World &world, const EditorViewportSettings &settings, float time
) {
    // HDR: the scene is linear 16F, the pipeline ALWAYS runs (at minimum an implicit Tonemap). The
    // display option of the Scene and camera.postprocessingEnabled control only the user chain;
    // the Game always shows the look of the game.
    const bool showChain = isGameLive() || settings.showPostprocessing;
    Entity cameraEntity = world.findMainCameraEntity();
    AssetRef<PostprocessingAsset> chain;
    PostprocessingFrameInputs inputs;
    inputs.color = m_targets.color();
    inputs.emissive = m_targets.emissive();
    inputs.depth = m_targets.depth();
    inputs.cameraNear = m_editorCamera.getNear();
    inputs.cameraFar = m_editorCamera.getFar();
    inputs.sunScreen = world.getFrameSunScreen(); // glare for bloom_apply
    if (cameraEntity.isValid()) {
        CameraComponent &camera = cameraEntity.getComponent<CameraComponent>();
        if (showChain && camera.postprocessingEnabled) { chain = camera.postprocessing.as<PostprocessingAsset>(); }
        inputs.bloom.enabled = showChain && camera.bloomEnabled;
        inputs.bloom.intensity = camera.bloomIntensity;
        inputs.bloom.radius = camera.bloomRadius;
        inputs.exposure.enabled = showChain && camera.autoExposureEnabled;
        inputs.exposure.key = camera.autoExposureKey;
        inputs.exposure.speed = camera.autoExposureSpeed;
        inputs.exposure.minExposure = camera.autoExposureMin;
        inputs.exposure.maxExposure = camera.autoExposureMax;
        if (!m_sceneCameraFrame) { // the frame is the game camera's
            inputs.cameraNear = camera.camera.getNear();
            inputs.cameraFar = camera.camera.getFar();
        }
    }
    if (!m_postprocessing) { m_postprocessing = std::make_unique<PostprocessingPipeline>(); }
    const Fern::Size dpi = Application::get()->getMainWindow()->getDpi();
    const Rect &frame = liveSurface().frame();
    const Miren::TextureHandle output = m_postprocessing->apply(
        chain, inputs, (uint32_t)(frame.size.width * dpi.width), (uint32_t)(frame.size.height * dpi.height), time
    );
    return output.id != inputs.color.id ? output : inputs.color; // a zero-size frame: the raw scene
}

} // namespace Panda
