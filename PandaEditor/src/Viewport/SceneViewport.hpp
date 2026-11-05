#pragma once

#include "Camera/CameraController.hpp"
#include "Viewport/Gizmos.hpp"
#include "Viewport/ViewportTargets.hpp"

#include <Miren/Miren.hpp>
#include <Panda/GameLogic/Camera.hpp>

#include <unordered_set>
#include <vector>

namespace Panda {

// The editor's side of the Scene viewport: the gizmo and the rect selection (mouse picking through
// the id buffer of the shared scene targets). The frame is the on-screen frame of the Scene tab;
// the render targets and the camera are owned by EditorViewports.
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

    SceneViewport(CameraController *cameraController, SceneRenderTargets *targets);
    ~SceneViewport();

    void setCamera(Camera *camera);
    void setWorld(World *world);
    void setFrame(Rect frame); // the on-screen frame of the Scene tab (points)
    Rect getFrame() const {
        return m_frame;
    }
    Gizmos &gizmo() { // the viewport panel drives the gizmo itself (EditorLayer::drivePandaUIGizmo → manipulate)
        return m_gizmos;
    }
    // Rect-select (and 1×1 single-click) driven from the PandaUI viewport. start/current are
    // viewport-local points; committed=true on mouse-up. Writes to SelectionContext.
    void driveRectSelection(IVec2 startPoint, IVec2 currentPoint, bool additive, bool committed);
    void pollSelection(); // apply async IDS reads queued by driveRectSelection (call each frame)

private:
    std::unordered_set<UUID> getEntitiesFromRequest(const ReadFrameBufferRequest &request);
    void readIdsMemoryIfNeed();
    void beginRectSelection(IVec2 viewportPoint, bool appendSelection);
    void updateRectSelection(IVec2 viewportPoint);
    void endRectSelection();
    void cancelRectSelection();
    void unselectAll();

    SceneRenderTargets *m_targets;
    Gizmos m_gizmos;
    Camera *m_camera;
    CameraController *m_cameraController;
    Rect m_frame;
    RectSelection m_rectSelection;
};

} // namespace Panda
