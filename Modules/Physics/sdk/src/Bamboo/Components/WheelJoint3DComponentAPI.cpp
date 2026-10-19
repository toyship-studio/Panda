#include "Bamboo/Components/WheelJoint3DComponentAPI.hpp"
#include "Bamboo/Internal/MainThread.hpp"
#include "Bamboo/Internal/PhysicsTable.hpp"

namespace Bamboo {

namespace WheelJoint3DComponentAPI {
    void setMotor(EntityHandle wheel, bool enabled, float speed, float maxTorque) {
        if (!Internal::requireMainThreadApi("WheelJoint3DComponentAPI::setMotor")) { return; }
        if (const auto *api = Internal::physicsTable()) {
            api->wheelJoint3DComponent_SetMotor(wheel.id, enabled ? 1 : 0, speed, maxTorque);
        }
    }

    float getSpinSpeed(EntityHandle wheel) {
        float speed = 0.f;
        if (!Internal::requireMainThreadApi("WheelJoint3DComponentAPI::getSpinSpeed")) { return speed; }
        if (const auto *api = Internal::physicsTable()) { api->wheelJoint3DComponent_GetSpinSpeed(wheel.id, &speed); }
        return speed;
    }

    void setSteeringAngle(EntityHandle wheel, float angle) {
        if (!Internal::requireMainThreadApi("WheelJoint3DComponentAPI::setSteeringAngle")) { return; }
        if (const auto *api = Internal::physicsTable()) { api->wheelJoint3DComponent_SetSteeringAngle(wheel.id, angle); }
    }

    float getSteeringAngle(EntityHandle wheel) {
        float angle = 0.f;
        if (!Internal::requireMainThreadApi("WheelJoint3DComponentAPI::getSteeringAngle")) { return angle; }
        if (const auto *api = Internal::physicsTable()) { api->wheelJoint3DComponent_GetSteeringAngle(wheel.id, &angle); }
        return angle;
    }

    void setSuspension(EntityHandle wheel, float hertz, float dampingRatio) {
        if (!Internal::requireMainThreadApi("WheelJoint3DComponentAPI::setSuspension")) { return; }
        if (const auto *api = Internal::physicsTable()) {
            api->wheelJoint3DComponent_SetSuspension(wheel.id, hertz, dampingRatio);
        }
    }
} // namespace WheelJoint3DComponentAPI

} // namespace Bamboo
