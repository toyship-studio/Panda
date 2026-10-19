//
// Created by Admin on 12.02.2022.
//

#include "CameraController.hpp"

#include <Panda/Math/Math.hpp>

namespace Panda {

CameraController::CameraController()
    : m_transform()
    , m_front()
    , m_up()
    , m_right()
    , m_target()
    , m_yaw(0.0f)
    , m_pitch(0.0f)
    , m_mouseSpeed(0.2f)
    , m_moveSpeed(20.f)
    , m_cursorStarted(false)
    , m_isMoveActive(false)
    , m_isRotationActive(false)
    , m_lastMouseX(0)
    , m_lastMouseY(0)
    , m_animation() {
    syncAnglesFromRotation(m_transform.getRotation());
}

void CameraController::update(float deltaTime) {
    if (m_animation.isActive) {
        float distanceToEnd = m_animation.endTransform.distanceTo(m_transform);
        if (distanceToEnd < 0.1) {
            m_transform.setTransform(m_animation.endTransform);
            m_animation.isActive = false;
            syncAnglesFromRotation(m_transform.getRotation());
            updateVectors();
        } else {
            static glm::vec3 velocity = glm::vec3(0.0);
            m_transform.setPosition(Math::smoothDamp(
                m_transform.getPosition(), m_animation.endTransform.getPosition(), velocity, 0.15, deltaTime
            ));
            float distanceFromStart = m_animation.startTransform.distanceTo(m_transform);
            float progress = distanceFromStart / m_animation.distance;
            m_transform.setRotation(glm::normalize(
                glm::slerp(m_animation.startTransform.getRotation(), m_animation.endTransform.getRotation(), progress)
            ));
            syncAnglesFromRotation(m_transform.getRotation());
            updateVectors();
        }
    }
    bool ctrl = Input::isKeyPressed(Fern::Key::KEY_LEFT_CONTROL) || Input::isKeyPressed(Fern::Key::KEY_RIGHT_CONTROL);
    bool shift = Input::isKeyPressed(Fern::Key::KEY_LEFT_SHIFT) || Input::isKeyPressed(Fern::Key::KEY_RIGHT_SHIFT);
    if (ctrl || shift) {
        m_cursorStarted = false;
        return;
    }
    if (m_isMoveActive) {
        float speed = m_moveSpeed * deltaTime;
        if (Input::isKeyPressed(Fern::Key::KEY_W)) { m_transform.translate(m_front * speed); }
        if (Input::isKeyPressed(Fern::Key::KEY_S)) { m_transform.translate(-m_front * speed); }
        if (Input::isKeyPressed(Fern::Key::KEY_A)) { m_transform.translate(-m_right * speed); }
        if (Input::isKeyPressed(Fern::Key::KEY_D)) { m_transform.translate(m_right * speed); }
    }
    if (!m_isRotationActive) {
        m_cursorStarted = false;
        return;
    }
    if (Input::isTrackpadScroll() || Input::isMouseButtonPressed(Fern::MouseButton::RIGHT)) {
        double deltaX;
        double deltaY;
        if (Input::isTrackpadScroll()) {
            deltaX = Input::getMouseScrollX() * 40;
            deltaY = Input::getMouseScrollY() * 40;
        } else {
            double mouseX = Input::getMousePositionX();
            double mouseY = Input::getMousePositionY();
            deltaX = mouseX - m_lastMouseX;
            deltaY = mouseY - m_lastMouseY;
            if (!m_cursorStarted) {
                m_cursorStarted = true;
                deltaX = 0;
                deltaY = 0;
            }
            m_lastMouseX = mouseX;
            m_lastMouseY = mouseY;
        }
        // DeltaX is the mouse offset in real time, so it does not need to be multiplied by deltaTime.
        // Real-world actions must not be multiplied by deltaTime; only game-world actions are.
        m_pitch += glm::radians(static_cast<float>(-deltaY * m_mouseSpeed));
        m_yaw += glm::radians(static_cast<float>(-deltaX * m_mouseSpeed));
        const float maxPitch = glm::radians(89.0f);
        m_pitch = glm::clamp(m_pitch, -maxPitch, maxPitch);
        syncRotationFromAngles();
    } else {
        m_cursorStarted = false;
    }

    double scroll = Input::getMouseScrollY();
    if (!Input::isTrackpadScroll() && scroll != 0) {
        float zoom = scroll * 0.5;
        m_transform.translate(zoom * m_front);
    }
}

void CameraController::updateVectors() {
    glm::mat4 rotationMatrix = m_transform.getRotationMatrix();
    m_front = rotationMatrix * glm::vec4(0.f, 0.f, -1.f, 1.f);
    m_right = rotationMatrix * glm::vec4(1.f, 0.f, 0.f, 1.f);
    m_up = rotationMatrix * glm::vec4(0.f, 1.f, 0.f, 1.f);
}

const glm::mat4 CameraController::getViewMatrix() {
    return glm::inverse(m_transform.getLocalTransform());
}

const glm::mat4 CameraController::getSkyViewMatrix() {
    return glm::inverse(m_transform.getSkyTransform());
}

glm::vec3 CameraController::getPosition() {
    return m_transform.getPosition();
}

void CameraController::setPosition(glm::vec3 position) {
    m_transform.setPosition(position);
}

glm::quat CameraController::getRotation() {
    return m_transform.getRotation();
}

void CameraController::setRotation(glm::quat quat) {
    syncAnglesFromRotation(quat);
}

void CameraController::setMoveActive(bool flag) {
    m_isMoveActive = flag;
}

void CameraController::setRotationActive(bool flag) {
    m_isRotationActive = flag;
}

void CameraController::reset(glm::vec3 pos) {
    TransformComponent newTransform;
    newTransform.setPosition({pos.x + 1.5, pos.y + 1.5, pos.z + 1.5});
    glm::quat r;
    r.x = -0.2911117374897003;
    r.y = 0.31903332471847534;
    r.z = 0.10366018861532211;
    r.w = 0.8959499597549438;
    newTransform.setRotation(r);
    animateTo(newTransform);
}

void CameraController::animateTo(TransformComponent transform) {
    Animation animation;
    animation.startTransform = m_transform;
    animation.endTransform = transform;
    animation.isActive = true;
    animation.distance = m_transform.distanceTo(transform);
    m_animation = animation;
}

void CameraController::syncRotationFromAngles() {
    m_transform.setRotation(glm::quat(glm::vec3(m_pitch, m_yaw, 0.0f)));
    updateVectors();
}

void CameraController::syncAnglesFromRotation(glm::quat rotation) {
    if (glm::length(rotation) < 0.0001f) { rotation = glm::quat(glm::vec3(0.0f)); }
    rotation = glm::normalize(rotation);

    const glm::vec3 forward = glm::normalize(rotation * glm::vec3(0.0f, 0.0f, -1.0f));
    m_pitch = asinf(glm::clamp(forward.y, -1.0f, 1.0f));
    m_yaw = atan2f(-forward.x, -forward.z);
    syncRotationFromAngles();
}

} // namespace Panda
