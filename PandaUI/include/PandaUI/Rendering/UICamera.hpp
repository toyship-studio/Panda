//
// Created by Michael Andreichev on 30.11.2023.
//

#pragma once

#include "PandaUI/Core/Base.hpp"

#include <glm/gtc/matrix_transform.hpp>

namespace PandaUI {

class UICamera final {
public:
    UICamera() {
        updateProjectionMatrix();
    }

    void setViewportSize(Size size) {
        m_viewportSize = size;
        updateProjectionMatrix();
    }

    const glm::mat4 &getProjection() const {
        return m_projection;
    }

private:
    void updateProjectionMatrix() {
        m_projection = glm::ortho(0.f, m_viewportSize.width, m_viewportSize.height, 0.f);
    }

    Size m_viewportSize = {300.f, 200.f};
    glm::mat4 m_projection = glm::mat4(1.f);
};

} // namespace PandaUI
