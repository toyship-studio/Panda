#include "Bamboo/Components/PrismaticJoint3DComponentAPI.hpp"
#include "Bamboo/Internal/MainThread.hpp"
#include "Bamboo/Internal/PhysicsTable.hpp"

namespace Bamboo {

namespace PrismaticJoint3DComponentAPI {
    void setMotor(EntityHandle entity, bool enabled, float speed, float maxForce) {
        if (!Internal::requireMainThreadApi("PrismaticJoint3DComponentAPI::setMotor")) { return; }
        if (const auto *api = Internal::physicsTable()) {
            api->prismaticJoint3D_SetMotor(entity.id, enabled ? 1u : 0u, speed, maxForce);
        }
    }

    void setTarget(EntityHandle entity, float translation) {
        if (!Internal::requireMainThreadApi("PrismaticJoint3DComponentAPI::setTarget")) { return; }
        if (const auto *api = Internal::physicsTable()) { api->prismaticJoint3D_SetTarget(entity.id, translation); }
    }

    float getTranslation(EntityHandle entity) {
        float translation = 0.f;
        float speed = 0.f;
        if (!Internal::requireMainThreadApi("PrismaticJoint3DComponentAPI::getTranslation")) { return translation; }
        if (const auto *api = Internal::physicsTable()) {
            api->prismaticJoint3D_GetTranslation(entity.id, &translation, &speed);
        }
        return translation;
    }

    float getSpeed(EntityHandle entity) {
        float translation = 0.f;
        float speed = 0.f;
        if (!Internal::requireMainThreadApi("PrismaticJoint3DComponentAPI::getSpeed")) { return speed; }
        if (const auto *api = Internal::physicsTable()) {
            api->prismaticJoint3D_GetTranslation(entity.id, &translation, &speed);
        }
        return speed;
    }
} // namespace PrismaticJoint3DComponentAPI

} // namespace Bamboo
