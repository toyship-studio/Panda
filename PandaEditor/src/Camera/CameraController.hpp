//
// Created by Admin on 12.02.2022.
//

#pragma once

#include <Panda.hpp>

namespace Panda {

// The fly camera of the Scene viewport. It reads no input itself: the Scene surface of the editor
// (a PandaUI view) collects what the user did over it this frame — the held WASD, the rotation of
// a right-button drag or a trackpad gesture, the zoom of the wheel — and hands it in as a
// Navigation. The input of the window (Panda::Input) belongs to the game.
class CameraController {
    struct Animation {
        bool isActive = false;
        TransformComponent startTransform;
        TransformComponent endTransform;
        float distance;
        float duration;
    };

public:
    struct Navigation {
        bool forward = false;
        bool backward = false;
        bool left = false;
        bool right = false;
        bool blocked = false;  // Shift or Control is held: a shortcut is being typed, not a flight
        glm::vec2 rotate{0.f}; // the rotation of the frame in points (a drag) or scaled scroll units
        float zoom = 0.f;      // the wheel of the frame, in notches
    };

    CameraController();
    void update(float deltaTime, const Navigation &navigation);
    const glm::mat4 getViewMatrix();
    const glm::mat4 getSkyViewMatrix();
    glm::vec3 getPosition();
    void setPosition(glm::vec3 position);
    glm::quat getRotation();
    void setRotation(glm::quat quat);
    void setMoveSpeed(float metersPerSecond) {
        m_moveSpeed = metersPerSecond;
    }
    void reset(glm::vec3 pos);
    void animateTo(TransformComponent transform);

private:
    void updateVectors();
    void syncRotationFromAngles();
    void syncAnglesFromRotation(glm::quat rotation);

    TransformComponent m_transform;
    Animation m_animation;
    glm::vec4 m_front;
    glm::vec4 m_up;
    glm::vec4 m_right;
    glm::vec3 m_target;
    float m_yaw;
    float m_pitch;
    float m_mouseSpeed;
    float m_moveSpeed;
};

} // namespace Panda
