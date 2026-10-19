//
// Created by Michael Andreichev on 06.10.2024.
//

#pragma once

#include "Camera/EditorCamera.hpp"
#include "Viewport/Gizmo/Gizmo.hpp"

#include <Panda/GameLogic/World.hpp>

namespace Panda {

class Gizmos final {
public:
    enum class Operation {
        Translate,
        Rotate,
        Scale,
    };

    enum class Space {
        Local,
        World,
    };

    explicit Gizmos(Camera *camera);

    // Engine-side core of the gizmo: input → manipulation of the selection → recording of an undo
    // command. Rendering of the geometry (getDrawData) is on the caller's side (the PandaUI overlay).
    // true = the gizmo is visible. viewMatrix — the view of the active viewport camera (in PLAY — the
    // game one, otherwise the editor one); nullptr = no view, the gizmo is hidden.
    bool manipulate(Rect viewportRect, const Gizmo::InputState &input, const glm::mat4 *viewMatrix);
    void setOperation(Operation operation) { m_operation = operation; } // from the PandaUI toolbar / keys 1-2-3
    void setSpace(Space space) { m_space = space; }
    void setWorld(World *world) {
        m_world = world;
    }

    void setCamera(Camera *camera) {
        m_camera = camera;
    }

    bool isUsing();
    bool isHovered() const;
    Operation getOperation() const {
        return m_operation;
    }
    Space getSpace() const {
        return m_space;
    }

private:
    Camera *m_camera;
    World *m_world;
    Operation m_operation;
    Space m_space;
    bool m_overlayHovered;
    bool m_gizmoVisible;
};

} // namespace Panda
