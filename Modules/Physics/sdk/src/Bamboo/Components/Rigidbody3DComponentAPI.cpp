#include "Bamboo/Components/Rigidbody3DComponentAPI.hpp"
#include "Bamboo/Internal/MainThread.hpp"
#include "Bamboo/Internal/PhysicsTable.hpp"

namespace Bamboo {

namespace Rigidbody3DComponentAPI {
    void applyForce(EntityHandle entity, Vec3 force) {
        if (!Internal::requireMainThreadApi("Rigidbody3DComponentAPI::applyForce")) { return; }
        if (const auto *api = Internal::physicsTable()) {
            api->rigidbody3DComponent_ApplyForce(entity.id, force.x, force.y, force.z);
        }
    }

    void applyLinearImpulse(EntityHandle entity, Vec3 impulse) {
        if (!Internal::requireMainThreadApi("Rigidbody3DComponentAPI::applyLinearImpulse")) { return; }
        if (const auto *api = Internal::physicsTable()) {
            api->rigidbody3DComponent_ApplyLinearImpulse(entity.id, impulse.x, impulse.y, impulse.z);
        }
    }

    void applyForceAtPoint(EntityHandle entity, Vec3 force, Vec3 point) {
        if (!Internal::requireMainThreadApi("Rigidbody3DComponentAPI::applyForceAtPoint")) { return; }
        if (const auto *api = Internal::physicsTable()) {
            api->rigidbody3DComponent_ApplyForceAtPoint(entity.id, force.x, force.y, force.z, point.x, point.y, point.z);
        }
    }

    void applyLinearImpulseAtPoint(EntityHandle entity, Vec3 impulse, Vec3 point) {
        if (!Internal::requireMainThreadApi("Rigidbody3DComponentAPI::applyLinearImpulseAtPoint")) { return; }
        if (const auto *api = Internal::physicsTable()) {
            api->rigidbody3DComponent_ApplyLinearImpulseAtPoint(
                entity.id, impulse.x, impulse.y, impulse.z, point.x, point.y, point.z
            );
        }
    }

    void applyTorque(EntityHandle entity, Vec3 torque) {
        if (!Internal::requireMainThreadApi("Rigidbody3DComponentAPI::applyTorque")) { return; }
        if (const auto *api = Internal::physicsTable()) {
            api->rigidbody3DComponent_ApplyTorque(entity.id, torque.x, torque.y, torque.z);
        }
    }

    void applyAngularImpulse(EntityHandle entity, Vec3 impulse) {
        if (!Internal::requireMainThreadApi("Rigidbody3DComponentAPI::applyAngularImpulse")) { return; }
        if (const auto *api = Internal::physicsTable()) {
            api->rigidbody3DComponent_ApplyAngularImpulse(entity.id, impulse.x, impulse.y, impulse.z);
        }
    }

    Vec3 getLinearVelocity(EntityHandle entity) {
        Vec3 result;
        if (!Internal::requireMainThreadApi("Rigidbody3DComponentAPI::getLinearVelocity")) { return result; }
        if (const auto *api = Internal::physicsTable()) {
            api->rigidbody3DComponent_GetLinearVelocity(entity.id, &result.x, &result.y, &result.z);
        }
        return result;
    }

    void setLinearVelocity(EntityHandle entity, Vec3 velocity) {
        if (!Internal::requireMainThreadApi("Rigidbody3DComponentAPI::setLinearVelocity")) { return; }
        if (const auto *api = Internal::physicsTable()) {
            api->rigidbody3DComponent_SetLinearVelocity(entity.id, velocity.x, velocity.y, velocity.z);
        }
    }

    Vec3 getAngularVelocity(EntityHandle entity) {
        Vec3 result;
        if (!Internal::requireMainThreadApi("Rigidbody3DComponentAPI::getAngularVelocity")) { return result; }
        if (const auto *api = Internal::physicsTable()) {
            api->rigidbody3DComponent_GetAngularVelocity(entity.id, &result.x, &result.y, &result.z);
        }
        return result;
    }

    void setAngularVelocity(EntityHandle entity, Vec3 velocity) {
        if (!Internal::requireMainThreadApi("Rigidbody3DComponentAPI::setAngularVelocity")) { return; }
        if (const auto *api = Internal::physicsTable()) {
            api->rigidbody3DComponent_SetAngularVelocity(entity.id, velocity.x, velocity.y, velocity.z);
        }
    }

    Vec3 getPointVelocity(EntityHandle entity, Vec3 point) {
        Vec3 result;
        if (!Internal::requireMainThreadApi("Rigidbody3DComponentAPI::getPointVelocity")) { return result; }
        if (const auto *api = Internal::physicsTable()) {
            api->rigidbody3DComponent_GetPointVelocity(
                entity.id, point.x, point.y, point.z, &result.x, &result.y, &result.z
            );
        }
        return result;
    }

    float getMass(EntityHandle entity) {
        float mass = 0.f;
        if (!Internal::requireMainThreadApi("Rigidbody3DComponentAPI::getMass")) { return mass; }
        if (const auto *api = Internal::physicsTable()) { api->rigidbody3DComponent_GetMass(entity.id, &mass); }
        return mass;
    }

    MassData getMassData(EntityHandle entity) {
        MassData result;
        if (!Internal::requireMainThreadApi("Rigidbody3DComponentAPI::getMassData")) { return result; }
        const auto *api = Internal::physicsTable();
        if (api == nullptr) { return result; }
        float center[3] = {};
        api->rigidbody3DComponent_GetMassData(entity.id, &result.mass, center, result.inverseInertia);
        result.center = Vec3(center[0], center[1], center[2]);
        return result;
    }
} // namespace Rigidbody3DComponentAPI

} // namespace Bamboo
