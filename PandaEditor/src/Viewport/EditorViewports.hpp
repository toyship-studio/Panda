#pragma once

#include "Camera/CameraController.hpp"
#include "Camera/EditorCamera.hpp"
#include "Project/Settings/EditorSettings.hpp" // EditorViewportSettings
#include "Viewport/EditorHelpersRenderer.hpp"
#include "Viewport/SceneGrid.hpp"
#include "Viewport/SceneViewport.hpp"
#include "Viewport/ViewportTargets.hpp"

#include <Panda/GameLogic/World.hpp>
#include <Panda/Renderer/PostprocessingPipeline.hpp>

#include <glm/glm.hpp>
#include <memory>

namespace Panda {

// The two viewports of the editor (context/architecture.md, "The Scene and the Game"):
//   Scene — the editor's view of the current world through the editor camera: selection, outline,
//           picking, gizmo, grid, helper icons and lines, the tools of the editor parts;
//   Game  — the frame of the game: the main camera of the world, the game UI and the game input,
//           no editor layer. In Play the running game, in Edit a still view of the main camera.
// The world is rendered ONCE per frame, through the viewport that is live: in Play the Game when
// its tab is visible, otherwise the Scene; in Edit the Scene when its tab is visible, otherwise the
// Game. The other viewport keeps its last frame (its panel says so). A second live camera would
// mean a second set of views and targets and a second fit of the camera-dependent state (CSM,
// exposure, terrain LOD, the occlusion window) — not worth it for a tab the user is not looking at.
enum class ViewportKind { Scene, Game };

// The on-screen frames of the viewport tabs this frame, from the PandaUI panels (in points); a tab
// that is not the selected one of its dock, or whose dock is hidden, is not visible.
struct ViewportFrames {
    Rect scene;
    bool sceneVisible = false;
    Rect game;
    bool gameVisible = false;
};

class EditorViewports final {
public:
    EditorViewports();

    void init(); // Miren is ready: the targets, the surfaces, the composite pass

    // Once a frame, before the world updates: the frames of the tabs → the surfaces, the live
    // viewport of the frame (the rule above), the shared targets at its size, the viewport frame
    // of Panda::Input (the Game's: the input of the window is the game's).
    void sync(const ViewportFrames &frames, bool playing);
    ViewportKind live() const {
        return m_live;
    }
    bool isSceneLive() const {
        return m_live == ViewportKind::Scene;
    }
    bool isGameLive() const {
        return m_live == ViewportKind::Game;
    }

    // The frame of the world through the live viewport. Scene: the editor camera, flown here by
    // what the Scene surface collected this frame (navigation). Game: the main camera of the
    // world, whose aspect follows the Game surface; a world without a main camera is seen through
    // the editor camera (gameHasCamera() tells the panel). simulate — physics and scripts run (Play).
    WorldFrame beginFrame(
        World &world, double deltaTime, bool simulate, const EditorViewportSettings &settings,
        const CameraController::Navigation &navigation
    );
    // After World::update of the same frame: the editor passes of the Scene (selection outline,
    // picking, grid, helper icons), the postprocessing chain of the main camera, the composite into
    // the output of the live surface. The game UI view is bound to the Game surface every frame.
    void finishFrame(World &world, const EditorViewportSettings &settings, float time);

    Miren::TextureHandle sceneResult() const {
        return m_sceneSurface.result();
    }
    Miren::TextureHandle gameResult() const {
        return m_gameSurface.result();
    }
    const Rect &sceneFrame() const {
        return m_sceneSurface.frame();
    }
    const Rect &gameFrame() const {
        return m_gameSurface.frame();
    }
    bool gameHasCamera() const { // the last Game frame found a main camera
        return m_gameHasCamera;
    }
    // The view-projection of the editor camera of this frame (tools, helper lines, the gizmo).
    const glm::mat4 &sceneViewProj() const {
        return m_sceneViewProj;
    }
    const glm::mat4 &sceneView() const {
        return m_sceneView;
    }

    SceneViewport &scene() {
        return m_scene;
    }
    Gizmos &gizmo() {
        return m_scene.gizmo();
    }
    CameraController &cameraController() {
        return m_cameraController;
    }
    EditorCamera &editorCamera() {
        return m_editorCamera;
    }

private:
    ViewportSurface &liveSurface() {
        return isSceneLive() ? m_sceneSurface : m_gameSurface;
    }
    void renderScenePasses(World &world, const EditorViewportSettings &settings);
    Miren::TextureHandle runPostprocessing(World &world, const EditorViewportSettings &settings, float time);

    SceneRenderTargets m_targets;
    ViewportComposite m_composite;
    ViewportSurface m_sceneSurface;
    ViewportSurface m_gameSurface;
    EditorCamera m_editorCamera;
    CameraController m_cameraController;
    SceneViewport m_scene;
    SceneGrid m_grid;
    EditorHelpersRenderer m_helpers;
    std::unique_ptr<PostprocessingPipeline> m_postprocessing;
    ViewportKind m_live = ViewportKind::Scene;
    WorldFrame m_frame;                   // the frame given to World::update (the picking pass repeats it)
    glm::mat4 m_sceneView = glm::mat4(1.f);
    glm::mat4 m_sceneViewProj = glm::mat4(1.f);
    bool m_sceneCameraFrame = true; // the frame is the editor camera's (Scene, or Game without a main camera)
    bool m_gameHasCamera = true;
};

} // namespace Panda
