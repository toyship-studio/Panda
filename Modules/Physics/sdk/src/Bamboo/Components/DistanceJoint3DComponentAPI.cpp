#include "Bamboo/Components/DistanceJoint3DComponentAPI.hpp"
#include "Bamboo/Internal/MainThread.hpp"
#include "Bamboo/Internal/PhysicsTable.hpp"

namespace Bamboo {

namespace DistanceJoint3DComponentAPI {
    void setLength(EntityHandle entity, float length) {
        if (!Internal::requireMainThreadApi("DistanceJoint3DComponentAPI::setLength")) { return; }
        if (const auto *api = Internal::physicsTable()) { api->distanceJoint3D_SetLength(entity.id, length); }
    }

    void setMotor(EntityHandle entity, bool enabled, float speed, float maxForce) {
        if (!Internal::requireMainThreadApi("DistanceJoint3DComponentAPI::setMotor")) { return; }
        if (const auto *api = Internal::physicsTable()) {
            api->distanceJoint3D_SetMotor(entity.id, enabled ? 1u : 0u, speed, maxForce);
        }
    }

    float getCurrentLength(EntityHandle entity) {
        float length = 0.f;
        if (!Internal::requireMainThreadApi("DistanceJoint3DComponentAPI::getCurrentLength")) { return length; }
        if (const auto *api = Internal::physicsTable()) { api->distanceJoint3D_GetCurrentLength(entity.id, &length); }
        return length;
    }
} // namespace DistanceJoint3DComponentAPI

} // namespace Bamboo
