#include "Bamboo/Components/MotorJoint3DComponentAPI.hpp"
#include "Bamboo/Internal/MainThread.hpp"
#include "Bamboo/Internal/PhysicsTable.hpp"

namespace Bamboo {

namespace MotorJoint3DComponentAPI {
    void setVelocities(EntityHandle entity, Vec3 linear, Vec3 angularDegreesPerSecond) {
        if (!Internal::requireMainThreadApi("MotorJoint3DComponentAPI::setVelocities")) { return; }
        if (const auto *api = Internal::physicsTable()) {
            api->motorJoint3D_SetVelocities(
                entity.id, linear.x, linear.y, linear.z, angularDegreesPerSecond.x, angularDegreesPerSecond.y,
                angularDegreesPerSecond.z
            );
        }
    }

    void setLimits(EntityHandle entity, float maxForce, float maxTorque) {
        if (!Internal::requireMainThreadApi("MotorJoint3DComponentAPI::setLimits")) { return; }
        if (const auto *api = Internal::physicsTable()) { api->motorJoint3D_SetLimits(entity.id, maxForce, maxTorque); }
    }
} // namespace MotorJoint3DComponentAPI

} // namespace Bamboo
