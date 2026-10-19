//
// Created by Admin on 11.02.2022.
//

#include "Panda/GameLogic/Components/WorldCamera.hpp"

#include <Foundation/GraphicsAPI.hpp>

namespace Panda {

WorldCamera::WorldCamera()
    : Camera({200, 300})
    , m_fieldOfView(70.f)
    , m_projectionType(ProjectionType::PERSPECTIVE)
    , m_orthoSize(1.f) {}

void WorldCamera::setFieldOfView(float degrees) {
    m_fieldOfView = degrees;
    if (m_projectionType == ProjectionType::PERSPECTIVE) { updateProjectionMatrix(); }
}

void WorldCamera::updateProjectionMatrix() {
    // Minimized window/zero viewport: aspect=inf poisons the projection with NaNs — skip,
    // a valid size will come with the next setViewportSize.
    if (m_viewportSize.width <= 0.f || m_viewportSize.height <= 0.f) { return; }
    float aspectRatio = m_viewportSize.width / m_viewportSize.height;
    // The NDC depth range for the backend: GL — [-1..1] (NO), Metal/D3D — [0..1] (ZO).
    // On Metal/D3D the NO variant loses the near part of the volume to clipping (for ortho —
    // exactly half, the scene disappears as a whole; for perspective — the slab at the near plane
    // and depth precision).
    constexpr bool homogeneousDepth = Foundation::ACTIVE_GRAPHICS_TRAITS.homogeneousDepth;
    switch (m_projectionType) {
        case ProjectionType::PERSPECTIVE: {
            if (homogeneousDepth) {
                m_projection = glm::perspective<float>(glm::radians(m_fieldOfView), aspectRatio, m_zNear, m_zFar);
            } else {
                m_projection = glm::perspectiveZO<float>(glm::radians(m_fieldOfView), aspectRatio, m_zNear, m_zFar);
            }
            break;
        }
        case ProjectionType::ORTHOGRAPHIC: {
            float orthoLeft = -m_orthoSize * aspectRatio * 0.5f;
            float orthoRight = m_orthoSize * aspectRatio * 0.5f;
            float orthoBottom = -m_orthoSize * 0.5f;
            float orthoTop = m_orthoSize * 0.5f;

            if (homogeneousDepth) {
                m_projection = glm::ortho(orthoLeft, orthoRight, orthoBottom, orthoTop, m_zNear, m_zFar);
            } else {
                m_projection = glm::orthoZO(orthoLeft, orthoRight, orthoBottom, orthoTop, m_zNear, m_zFar);
            }
            break;
        }
    }
}

WorldCamera::ProjectionType WorldCamera::getProjectionType() const {
    return m_projectionType;
}

void WorldCamera::setProjectionType(WorldCamera::ProjectionType type) {
    m_projectionType = type;
    updateProjectionMatrix();
}

void WorldCamera::setOrthoSize(float orthoSize) {
    m_orthoSize = orthoSize;
    if (m_projectionType == ProjectionType::ORTHOGRAPHIC) { updateProjectionMatrix(); }
}

float WorldCamera::getFieldOfView() {
    return m_fieldOfView;
}

float WorldCamera::getOrthoSize() {
    return m_orthoSize;
}

} // namespace Panda
