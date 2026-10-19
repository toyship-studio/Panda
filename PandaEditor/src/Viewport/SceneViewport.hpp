//
// Created by Michael Andreichev on 14.09.2023.
//

#pragma once

#include "Viewport/Gizmos.hpp"
#include "Camera/CameraController.hpp"

#include <Viewport/Viewport.hpp>
#include <Panda/GameLogic/Camera.hpp>
#include <Miren/Miren.hpp>

namespace Panda {

class SceneViewport final {
public:
    struct ReadFrameBufferRequest {
        int ready;
        Foundation::Memory mem;
        // Multiplied by screen content scale
        ISize bufferSize;
    };

    struct RectSelection {
        bool isStarted = false;
        bool appendSelection = false;
        IRect rect;
        std::unordered_set<UUID> initialSelection;
        std::unordered_set<UUID> currentSelection;
        std::vector<ReadFrameBufferRequest> readRequests;

        bool isEqualTo(IRect rect);
    };

    explicit SceneViewport(CameraController *cameraController);
    ~SceneViewport();

    void initWithSize(Vec2 size);
    void setCamera(Camera *camera);
    void setWorld(World *world);
    void drawOutline(float dt);
    void focus();
    bool isFocused();
    bool isHovered();
    Rect getFrame();
    // Seam #1: drive the engine viewport from an externally-provided on-screen frame (the PandaUI viewport panel).
    void syncExternalFrame(Rect frame);
    Miren::TextureHandle getResultTexture() {
        return m_viewport.getResultTexture();
    }
    Miren::TextureHandle getSceneColorTexture() {
        return m_viewport.getSceneColorTexture();
    }
    Miren::TextureHandle getSceneEmissiveTexture() {
        return m_viewport.getSceneEmissiveTexture();
    }
    Miren::TextureHandle getSceneDepthTexture() {
        return m_viewport.getSceneDepthTexture();
    }
    void setPostprocessedColor(Miren::TextureHandle texture) {
        m_viewport.setPostprocessedColor(texture);
    }
    Gizmos &gizmo() { // the viewport panel drives the gizmo itself (EditorLayer::drivePandaUIGizmo → manipulate)
        return m_gizmos;
    }
    // Seam #2: rect-select (and 1×1 single-click) driven from the PandaUI viewport.
    // start/current are viewport-local points; committed=true on mouse-up. Writes to SelectionContext.
    void driveRectSelection(IVec2 startPoint, IVec2 currentPoint, bool additive, bool committed);
    void pollSelection(); // apply async IDS reads queued by driveRectSelection (call each frame)

private:
    void updateViewportSize(Size size);
    std::unordered_set<UUID> getEntitiesFromRequest(const ReadFrameBufferRequest &request);
    void readIdsMemoryIfNeed();
    void beginRectSelection(IVec2 viewportPoint, bool appendSelection);
    void updateRectSelection(IVec2 viewportPoint);
    void endRectSelection();
    void cancelRectSelection();
    void unselectAll();

    Viewport m_viewport;
    Gizmos m_gizmos;
    Camera *m_camera;
    CameraController *m_cameraController;
    bool m_focusNextFrame;
    bool m_focused;
    bool m_hovered;
    RectSelection m_rectSelection;
};

} // namespace Panda
