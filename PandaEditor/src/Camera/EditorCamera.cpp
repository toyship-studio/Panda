#include "EditorCamera.hpp"

#include <Foundation/GraphicsAPI.hpp>
#include <glm/ext.hpp>

namespace Panda {

EditorCamera::EditorCamera()
    : Camera({200, 200})
    , m_view(1.f)
    , m_fieldOfView(70.f) {}

void EditorCamera::updateProjectionMatrix() {
    // Minimized window / zero-size viewport: aspect=inf poisons the projection with NaNs.
    if (m_viewportSize.width <= 0.f || m_viewportSize.height <= 0.f) { return; }
    float aspectRatio = m_viewportSize.width / m_viewportSize.height;
    // ZO variant for Metal/D3D (NDC z in [0..1]): the NO variant loses a slab at the near plane to
    // clipping, plus half of the depth buffer precision.
    if (Foundation::ACTIVE_GRAPHICS_TRAITS.homogeneousDepth) {
        m_projection = glm::perspective<float>(glm::radians(m_fieldOfView), aspectRatio, m_zNear, m_zFar);
    } else {
        m_projection = glm::perspectiveZO<float>(glm::radians(m_fieldOfView), aspectRatio, m_zNear, m_zFar);
    }
}

} // namespace Panda
