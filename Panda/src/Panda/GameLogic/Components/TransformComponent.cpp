//
// Created by Admin on 09.02.2022.
//

#include "Panda/GameLogic/Components/TransformComponent.hpp"
#include "Panda/Math/Math.hpp"

#include <glm/gtx/matrix_decompose.hpp>

namespace Panda {

TransformComponent::TransformComponent()
    : rotation(1.0f, 0.0f, 0.0f, 0.0f)
    , position(0.0f, 0.0f, 0.0f)
    , scale(1.0f, 1.0f, 1.0f) {}

void TransformComponent::setRotationEuler(glm::vec3 degrees) {
    rotation = glm::normalize(glm::quat(glm::radians(degrees)));
}

void TransformComponent::rotateEuler(glm::vec3 degrees) {
    glm::vec3 currentEuler = glm::eulerAngles(rotation);
    rotation = glm::normalize(glm::quat(currentEuler + glm::radians(degrees)));
}

glm::vec3 TransformComponent::getRotationEuler() const {
    return glm::degrees(glm::eulerAngles(rotation));
}

void TransformComponent::setRotation(glm::quat quat) {
    rotation = glm::normalize(quat);
}

glm::quat TransformComponent::getRotation() const {
    return rotation;
}

glm::mat4 TransformComponent::getRotationMatrix() {
    return glm::toMat4(rotation);
}

float TransformComponent::distanceTo(TransformComponent tc) {
    return glm::distance(position, tc.position);
}

void TransformComponent::translate(glm::vec3 offset) {
    position += offset;
}

void TransformComponent::setPosition(glm::vec3 pos) {
    position = pos;
}

glm::vec3 TransformComponent::getPosition() const {
    return position;
}

glm::vec4 TransformComponent::getPositionHomogeneous() {
    return {position.x, position.y, position.z, 1.0f};
}

glm::mat4 TransformComponent::getLocalTransform() const {
    return glm::translate(glm::mat4(1.0f), position) * glm::toMat4(rotation) * glm::scale(glm::mat4(1.0f), scale);
}

glm::mat4 TransformComponent::getSkyTransform() const {
    return glm::toMat4(rotation) * glm::scale(glm::mat4(1.0f), scale);
}

void TransformComponent::setTransform(const glm::mat4 &transform) {
    glm::vec3 skew;
    glm::vec4 perspective;
    glm::quat decomposedRotation;
    if (glm::decompose(transform, scale, decomposedRotation, position, skew, perspective)) {
        rotation = glm::normalize(decomposedRotation);
    }
}

glm::vec3 TransformComponent::getScale() const {
    return scale;
}

void TransformComponent::setScale(glm::vec3 _scale) {
    scale = _scale;
}

void TransformComponent::setTransform(TransformComponent &other) {
    position = other.position;
    rotation = other.rotation;
    scale = other.scale;
}

} // namespace Panda
