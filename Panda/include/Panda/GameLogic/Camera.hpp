//
// Created by Michael Andreichev on 30.11.2023.
//

#pragma once

#include <glm/glm.hpp>

#include "Panda/Base/Base.hpp"

namespace Panda {

class Camera {
public:
    Camera(Size viewportSize);
    virtual ~Camera() = default;
    virtual void updateProjectionMatrix() = 0;

    glm::mat4 getProjection();
    Size getViewportSize();
    void setViewportSize(Size size);
    float getNear();
    void setNear(float near);
    float getFar();
    void setFar(float far);

    // MARK: Math
    /// First variant: the distance from the camera is given in world coordinates
    Vec3 screenCoordToWorld(glm::mat4 view, Vec2 screen, float distance);
    /// Second variant: the distance from the camera is given by the depth buffer.
    /// The z value is from -1 to 1 for OpenGL or from 0 to 1 for Vulkan.
    Vec3 screenCoordToWorld(glm::mat4 view, Vec3 screen);

protected:
    glm::mat4 m_projection;
    Size m_viewportSize;
    float m_zNear;
    float m_zFar;
};

} // namespace Panda
