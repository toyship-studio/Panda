#include "Bamboo/Components/Rigidbody2DComponentAPI.hpp"
#include "Bamboo/Internal/MainThread.hpp"
#include "Bamboo/Internal/PhysicsTable.hpp"

namespace Bamboo {

namespace Rigidbody2DComponentAPI {
    void applyForce(EntityHandle entity, Vec2 force) {
        if (!Internal::requireMainThreadApi("Rigidbody2DComponentAPI::applyForce")) { return; }
        if (const auto *api = Internal::physicsTable()) { api->rigidbody2DComponent_ApplyForce(entity.id, force.x, force.y); }
    }

    void applyLinearImpulse(EntityHandle entity, Vec2 impulse) {
        if (!Internal::requireMainThreadApi("Rigidbody2DComponentAPI::applyLinearImpulse")) { return; }
        if (const auto *api = Internal::physicsTable()) {
            api->rigidbody2DComponent_ApplyLinearImpulse(entity.id, impulse.x, impulse.y);
        }
    }

    Vec2 getLinearVelocity(EntityHandle entity) {
        Vec2 result;
        if (!Internal::requireMainThreadApi("Rigidbody2DComponentAPI::getLinearVelocity")) { return result; }
        if (const auto *api = Internal::physicsTable()) {
            api->rigidbody2DComponent_GetLinearVelocity(entity.id, &result.x, &result.y);
        }
        return result;
    }

    void setLinearVelocity(EntityHandle entity, Vec2 velocity) {
        if (!Internal::requireMainThreadApi("Rigidbody2DComponentAPI::setLinearVelocity")) { return; }
        if (const auto *api = Internal::physicsTable()) {
            api->rigidbody2DComponent_SetLinearVelocity(entity.id, velocity.x, velocity.y);
        }
    }

    float getMass(EntityHandle entity) {
        float mass = 0.f;
        if (!Internal::requireMainThreadApi("Rigidbody2DComponentAPI::getMass")) { return mass; }
        if (const auto *api = Internal::physicsTable()) { api->rigidbody2DComponent_GetMass(entity.id, &mass); }
        return mass;
    }

    float getFriction(EntityHandle entity) {
        float friction = 0.f;
        if (!Internal::requireMainThreadApi("Rigidbody2DComponentAPI::getFriction")) { return friction; }
        if (const auto *api = Internal::physicsTable()) { api->rigidbody2DComponent_GetFriction(entity.id, &friction); }
        return friction;
    }

    void setFriction(EntityHandle entity, float friction) {
        if (!Internal::requireMainThreadApi("Rigidbody2DComponentAPI::setFriction")) { return; }
        if (const auto *api = Internal::physicsTable()) { api->rigidbody2DComponent_SetFriction(entity.id, friction); }
    }
} // namespace Rigidbody2DComponentAPI

} // namespace Bamboo
