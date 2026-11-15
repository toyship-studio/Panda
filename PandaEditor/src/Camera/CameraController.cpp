#include "CameraController.hpp"

#include <Panda/Math/Math.hpp>

#include <algorithm>
#include <cmath>

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
    , m_animation() {
    syncAnglesFromRotation(m_transform.getRotation());
}

void CameraController::update(float deltaTime, const Navigation &navigation) {
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
    if (navigation.blocked) { return; } // Shift / Control: the keys belong to a shortcut
    const float speed = m_moveSpeed * deltaTime;
    if (navigation.forward) { m_transform.translate(m_front * speed); }
    if (navigation.backward) { m_transform.translate(-m_front * speed); }
    if (navigation.left) { m_transform.translate(-m_right * speed); }
    if (navigation.right) { m_transform.translate(m_right * speed); }
    if (navigation.rotate.x != 0.f || navigation.rotate.y != 0.f) {
        // The rotation is a mouse offset in real time: it is not scaled by deltaTime (only
        // game-world actions are).
        m_pitch += glm::radians(-navigation.rotate.y * m_mouseSpeed);
        m_yaw += glm::radians(-navigation.rotate.x * m_mouseSpeed);
        const float maxPitch = glm::radians(89.0f);
        m_pitch = glm::clamp(m_pitch, -maxPitch, maxPitch);
        syncRotationFromAngles();
    }
    if (navigation.zoom != 0.f) { m_transform.translate(navigation.zoom * 0.5f * m_front); }
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
    m_animation.isActive = false; // a pose set from outside ends the flight in progress
    m_transform.setPosition(position);
}

glm::quat CameraController::getRotation() {
    return m_transform.getRotation();
}

void CameraController::setRotation(glm::quat quat) {
    m_animation.isActive = false;
    syncAnglesFromRotation(quat);
}

glm::vec3 CameraController::getForward() {
    return glm::vec3(m_front);
}

void CameraController::lookAt(glm::vec3 target) {
    const glm::vec3 offset = target - m_transform.getPosition();
    if (glm::length(offset) < 0.0001f) { return; }
    const glm::vec3 direction = glm::normalize(offset);
    // The same angles as syncAnglesFromRotation reads back: the camera keeps its roll at zero.
    const float maxPitch = glm::radians(89.0f);
    m_animation.isActive = false;
    m_pitch = glm::clamp(asinf(glm::clamp(direction.y, -1.0f, 1.0f)), -maxPitch, maxPitch);
    m_yaw = atan2f(-direction.x, -direction.z);
    syncRotationFromAngles();
}

void CameraController::frame(glm::vec3 center, float radius, float fieldOfView) {
    // The view the editor has always looked at a thing from: from the front right, a little above.
    const glm::quat rotation(0.8959499597549438f, -0.2911117374897003f, 0.31903332471847534f, 0.10366018861532211f);
    const float distance = std::max(radius, 0.05f) / std::sin(std::clamp(fieldOfView, 0.1f, 3.f) * 0.5f) * 1.15f;
    TransformComponent target;
    target.setRotation(rotation);
    target.setPosition(center - (rotation * glm::vec3(0.f, 0.f, -1.f)) * distance);
    animateTo(target);
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
