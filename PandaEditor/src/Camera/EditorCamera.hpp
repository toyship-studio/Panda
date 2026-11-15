#pragma once

#include <Panda/GameLogic/Camera.hpp>

namespace Panda {

class EditorCamera : public Camera {
public:
    EditorCamera();
    ~EditorCamera() = default;
    // The vertical field of view, radians: what fits the frame from a distance (CameraController::frame).
    float getFieldOfView() const {
        return m_fieldOfView * 0.017453292f;
    }

private:
    void updateProjectionMatrix() override;

    glm::mat4 m_view;
    float m_fieldOfView;
};

} // namespace Panda