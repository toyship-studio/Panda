#pragma once

#include "Camera/CameraController.hpp"
#include "Camera/EditorCamera.hpp"
#include "Project/Settings/EditorSettings.hpp" // EditorViewportSettings
#include "Viewport/EditorHelpersRenderer.hpp"
#include "Viewport/SceneGrid.hpp"
#include "Viewport/SceneViewport.hpp"
#include "Viewport/ViewportTargets.hpp"

#include <Panda/GameLogic/World.hpp>
#include <Panda/Renderer/WorldView.hpp>

#include <glm/glm.hpp>

namespace Panda {

// The two viewports of the editor (context/architecture.md, "The Scene and the Game"):
//   Scene — the editor's view of the current world through the editor camera: selection, outline,
//           picking, gizmo, grid, helper icons and lines, the tools of the editor parts;
//   Game  — the frame of the game: the main camera of the world, the game UI and the game input,
//           no editor layer. In Play the running game, in Edit a still view of the main camera.
// The world is rendered ONCE per frame, through the viewport that is live: the one the user works
// in — the viewport in focus — when its tab is on screen, otherwise the other one. The viewport
// that is not live keeps its last frame, and its panel says so (a veil over the picture). A second
// live camera would mean a second view of the world — its targets and passes, a second fit of the
// camera-dependent state (CSM, exposure, terrain LOD, the occlusion window) — every frame, for a
// picture the user is not working with.
enum class ViewportKind { Scene, Game };

// The on-screen frames of the viewport tabs this frame, from the PandaUI panels (in points); a tab
// that is not the selected one of its dock, or whose dock is hidden, is not visible. focused — the
// viewport the user works in (the one whose surface had the focus last; Play gives it to the Game,
// Stop to the Scene).
struct ViewportFrames {
    Rect scene;
    bool sceneVisible = false;
    Rect game;
    bool gameVisible = false;
    ViewportKind focused = ViewportKind::Scene;
};

class EditorViewports final {
public:
    EditorViewports();

    void init(); // Miren is ready: the targets, the surfaces, the composite pass

    // Once a frame, before the world updates: the frames of the tabs → the surfaces, the live
    // viewport of the frame (the rule above), the view and the targets of the editor passes at
    // its size, the viewport frame of Panda::Input (the Game's: the input of the window is the
    // game's).
    void sync(const ViewportFrames &frames);
    ViewportKind live() const {
        return m_live;
    }
    bool isSceneLive() const {
        return m_live == ViewportKind::Scene;
    }
    bool isGameLive() const {
        return m_live == ViewportKind::Game;
    }

    // Before the world is simulated: the camera of the frame is chosen. Scene: the editor camera,
    // flown here by what the Scene surface collected this frame (navigation). Game: the main
    // camera of the world, whose aspect follows the Game surface; a world without a main camera is
    // seen through the editor camera (gameHasCamera() tells the panel).
    void beginFrame(
        World &world, double deltaTime, const EditorViewportSettings &settings,
        const CameraController::Navigation &navigation
    );
    // After the simulation of the same frame: the world through the live viewport (WorldView) —
    // its shadows and the scene — then the editor passes of the Scene (selection outline, picking,
    // grid, helper icons), the look of the main camera (WorldView::develop), the composite into
    // the output of the live surface. The game UI is drawn over the output of the Game after
    // this (gameTarget).
    void render(World &world, const EditorViewportSettings &settings, float time);
    // The view the world is drawn through: one for both viewports — one of them renders a frame.
    WorldView &worldView() {
        return m_view;
    }

    Miren::TextureHandle sceneResult() const {
        return m_sceneSurface.result();
    }
    Miren::TextureHandle gameResult() const {
        return m_gameSurface.result();
    }
    const Rect &gameFrame() const {
        return m_gameSurface.frame();
    }
    // What the game UI is drawn into, over the frame of the game: the output of the Game while the
    // Game renders; invalid — the Game shows a frame that is not this one's, the UI is not drawn.
    Miren::FrameBufferHandle gameTarget() const {
        return isGameLive() ? m_gameSurface.frameBuffer() : Miren::FrameBufferHandle();
    }
    bool gameHasCamera() const { // the last Game frame found a main camera
        return m_gameHasCamera;
    }
    // The output of a viewport: its texture and its size in pixels (a screenshot reads it).
    const ViewportSurface &surface(ViewportKind kind) const {
        return kind == ViewportKind::Scene ? m_sceneSurface : m_gameSurface;
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
    // The frame as the viewport shows it: the look of the main camera of the world — in the Scene
    // its chain, bloom and auto exposure only with the display option on.
    Miren::TextureHandle develop(World &world, const EditorViewportSettings &settings, float time);

    EditorPassTargets m_passTargets;
    ViewportComposite m_composite;
    ViewportSurface m_sceneSurface;
    ViewportSurface m_gameSurface;
    EditorCamera m_editorCamera;
    CameraController m_cameraController;
    SceneViewport m_scene;
    SceneGrid m_grid;
    EditorHelpersRenderer m_helpers;
    WorldView m_view; // the world through the camera of the live viewport, at its size
    ViewportKind m_live = ViewportKind::Scene;
    ViewCamera m_camera;           // of the frame: the editor camera's, or the main camera's (asked after the simulation)
    bool m_mainCameraFrame = false; // the frame is seen through the main camera of the world
    glm::mat4 m_sceneView = glm::mat4(1.f);
    glm::mat4 m_sceneViewProj = glm::mat4(1.f);
    bool m_sceneCameraFrame = true; // the frame is the editor camera's (Scene, or Game without a main camera)
    bool m_gameHasCamera = true;
};

} // namespace Panda
