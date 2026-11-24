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

// The two viewports of the editor (context/architecture.md, "The World view and the Game"):
//   Scene — the World view: the current world through the editor camera, with the selection, the
//           outline, picking, the gizmo, the grid, the helper icons and lines, the tools of the
//           editor parts;
//   Game  — the frame of the game: the main camera of the world, the game UI and the game input,
//           no editor layer. In Play the running game, in Edit a still view of the main camera.
// The middle of a world tab shows one of them, and the world is rendered ONCE per frame, through
// that one (the live viewport) — while it is on screen: with another tab in front nobody looks at
// the world, and it is not drawn. A second live camera would mean a second view of the world —
// its targets and passes, a second fit of the camera-dependent state (CSM, exposure, terrain LOD,
// the occlusion window) — every frame.
enum class ViewportKind { Scene, Game };

// The frames of the two surfaces this frame, from the PandaUI views (in points); `shown` — the
// one the middle of the world tab shows; `visible` — it is on screen (the tab of the active world
// is the selected one of its dock).
struct ViewportFrames {
    Rect scene;
    Rect game;
    ViewportKind shown = ViewportKind::Scene;
    bool visible = false;
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
    // The live viewport is on screen: somebody looks at the frame of the world.
    bool isOnScreen() const {
        return m_onScreen;
    }
    // The Game is on screen: the pointer over its surface and the keys of its focus are the game's.
    // Behind another tab the place of the surface belongs to what covers it.
    bool isGameOnScreen() const {
        return isGameLive() && m_onScreen;
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
    // The draw counters of the frame of the world: zero while it is not drawn (isOnScreen) — the
    // renderers still hold the numbers of the last frame they drew.
    Renderer3D::Statistics statistics3D() {
        return m_onScreen ? m_view.renderer3D().getStats() : Renderer3D::Statistics{};
    }
    Renderer2D::Statistics statistics2D() {
        return m_onScreen ? m_view.renderer2D().getStats() : Renderer2D::Statistics{};
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
    bool m_onScreen = false; // the live viewport is on screen this frame
    ViewCamera m_camera;           // of the frame: the editor camera's, or the main camera's (asked after the simulation)
    bool m_mainCameraFrame = false; // the frame is seen through the main camera of the world
    glm::mat4 m_sceneView = glm::mat4(1.f);
    glm::mat4 m_sceneViewProj = glm::mat4(1.f);
    bool m_sceneCameraFrame = true; // the frame is the editor camera's (Scene, or Game without a main camera)
    bool m_gameHasCamera = true;
};

} // namespace Panda
