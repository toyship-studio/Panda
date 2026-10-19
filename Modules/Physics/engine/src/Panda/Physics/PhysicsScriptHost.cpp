#include "Panda/Physics/PhysicsExtensions.hpp"
#include "Panda/Physics/Physics2D.hpp"
#include "Panda/Physics/Physics3D.hpp"
#include "Panda/Physics/PhysicsScriptApi.hpp"

#include <Panda/Extensions/ScriptHostApis.hpp>
#include <Panda/GameLogic/GameContext.hpp>
#include <Panda/GameLogic/World.hpp>
#include <Panda/ScriptEngine/InnerScriptHook.hpp> // resolveScriptEntity

namespace Panda::Physics {

namespace {

    // The simulation of the entity's world; nullptr — the handle is broken (reported by
    // resolveScriptEntity) or the world does not run: the call becomes a no-op with zeros.
    Physics2D *physics2DOf(Handle entityHandle, Entity &outEntity) {
        outEntity = resolveScriptEntity(entityHandle);
        return outEntity.isValid() ? outEntity.getWorld()->findContext<Physics2D>() : nullptr;
    }

    Physics3D *physics3DOf(Handle entityHandle, Entity &outEntity) {
        outEntity = resolveScriptEntity(entityHandle);
        return outEntity.isValid() ? outEntity.getWorld()->findContext<Physics3D>() : nullptr;
    }

    /// RIGIDBODY2D COMPONENT

    void rigidbody2DComponent_ApplyForce(Handle entityHandle, float x, float y) {
        Entity entity;
        if (Physics2D *physics = physics2DOf(entityHandle, entity)) { physics->applyForce(entity, {x, y}); }
    }

    void rigidbody2DComponent_ApplyLinearImpulse(Handle entityHandle, float x, float y) {
        Entity entity;
        if (Physics2D *physics = physics2DOf(entityHandle, entity)) { physics->applyLinearImpulse(entity, {x, y}); }
    }

    void rigidbody2DComponent_GetLinearVelocity(Handle entityHandle, float *x, float *y) {
        Entity entity;
        Vec2 velocity;
        if (Physics2D *physics = physics2DOf(entityHandle, entity)) { velocity = physics->getLinearVelocity(entity); }
        *x = velocity.x;
        *y = velocity.y;
    }

    void rigidbody2DComponent_SetLinearVelocity(Handle entityHandle, float x, float y) {
        Entity entity;
        if (Physics2D *physics = physics2DOf(entityHandle, entity)) { physics->setLinearVelocity(entity, {x, y}); }
    }

    void rigidbody2DComponent_GetMass(Handle entityHandle, float *mass) {
        Entity entity;
        *mass = 0.f;
        if (Physics2D *physics = physics2DOf(entityHandle, entity)) { *mass = physics->getMass(entity); }
    }

    void rigidbody2DComponent_GetFriction(Handle entityHandle, float *friction) {
        Entity entity;
        *friction = 0.f;
        if (Physics2D *physics = physics2DOf(entityHandle, entity)) { *friction = physics->getFriction(entity); }
    }

    void rigidbody2DComponent_SetFriction(Handle entityHandle, float friction) {
        Entity entity;
        if (Physics2D *physics = physics2DOf(entityHandle, entity)) { physics->setFriction(entity, friction); }
    }

    /// RIGIDBODY3D COMPONENT

    void rigidbody3DComponent_ApplyForce(Handle entityHandle, float x, float y, float z) {
        Entity entity;
        if (Physics3D *physics = physics3DOf(entityHandle, entity)) { physics->applyForce(entity, {x, y, z}); }
    }

    void rigidbody3DComponent_ApplyLinearImpulse(Handle entityHandle, float x, float y, float z) {
        Entity entity;
        if (Physics3D *physics = physics3DOf(entityHandle, entity)) { physics->applyLinearImpulse(entity, {x, y, z}); }
    }

    void rigidbody3DComponent_GetLinearVelocity(Handle entityHandle, float *x, float *y, float *z) {
        Entity entity;
        Vec3 velocity;
        if (Physics3D *physics = physics3DOf(entityHandle, entity)) { velocity = physics->getLinearVelocity(entity); }
        *x = velocity.x;
        *y = velocity.y;
        *z = velocity.z;
    }

    void rigidbody3DComponent_SetLinearVelocity(Handle entityHandle, float x, float y, float z) {
        Entity entity;
        if (Physics3D *physics = physics3DOf(entityHandle, entity)) { physics->setLinearVelocity(entity, {x, y, z}); }
    }

    void rigidbody3DComponent_GetMass(Handle entityHandle, float *mass) {
        Entity entity;
        *mass = 0.f;
        if (Physics3D *physics = physics3DOf(entityHandle, entity)) { *mass = physics->getMass(entity); }
    }

    void rigidbody3DComponent_ApplyForceAtPoint(
        Handle entityHandle, float x, float y, float z, float pointX, float pointY, float pointZ
    ) {
        Entity entity;
        if (Physics3D *physics = physics3DOf(entityHandle, entity)) {
            physics->applyForceAtPoint(entity, {x, y, z}, {pointX, pointY, pointZ});
        }
    }

    void rigidbody3DComponent_ApplyLinearImpulseAtPoint(
        Handle entityHandle, float x, float y, float z, float pointX, float pointY, float pointZ
    ) {
        Entity entity;
        if (Physics3D *physics = physics3DOf(entityHandle, entity)) {
            physics->applyLinearImpulseAtPoint(entity, {x, y, z}, {pointX, pointY, pointZ});
        }
    }

    void rigidbody3DComponent_ApplyTorque(Handle entityHandle, float x, float y, float z) {
        Entity entity;
        if (Physics3D *physics = physics3DOf(entityHandle, entity)) { physics->applyTorque(entity, {x, y, z}); }
    }

    void rigidbody3DComponent_ApplyAngularImpulse(Handle entityHandle, float x, float y, float z) {
        Entity entity;
        if (Physics3D *physics = physics3DOf(entityHandle, entity)) { physics->applyAngularImpulse(entity, {x, y, z}); }
    }

    void rigidbody3DComponent_GetAngularVelocity(Handle entityHandle, float *x, float *y, float *z) {
        Entity entity;
        Vec3 velocity;
        if (Physics3D *physics = physics3DOf(entityHandle, entity)) { velocity = physics->getAngularVelocity(entity); }
        *x = velocity.x;
        *y = velocity.y;
        *z = velocity.z;
    }

    void rigidbody3DComponent_SetAngularVelocity(Handle entityHandle, float x, float y, float z) {
        Entity entity;
        if (Physics3D *physics = physics3DOf(entityHandle, entity)) { physics->setAngularVelocity(entity, {x, y, z}); }
    }

    void rigidbody3DComponent_GetPointVelocity(
        Handle entityHandle, float pointX, float pointY, float pointZ, float *x, float *y, float *z
    ) {
        Entity entity;
        Vec3 velocity;
        if (Physics3D *physics = physics3DOf(entityHandle, entity)) {
            velocity = physics->getPointVelocity(entity, {pointX, pointY, pointZ});
        }
        *x = velocity.x;
        *y = velocity.y;
        *z = velocity.z;
    }

    void rigidbody3DComponent_GetMassData(Handle entityHandle, float *mass, float *center, float *inverseInertia) {
        Entity entity;
        MassData3D data;
        if (Physics3D *physics = physics3DOf(entityHandle, entity)) { data = physics->getMassData(entity); }
        *mass = data.mass;
        center[0] = data.center.x;
        center[1] = data.center.y;
        center[2] = data.center.z;
        for (int column = 0; column < 3; column++) {
            for (int row = 0; row < 3; row++) {
                inverseInertia[column * 3 + row] = data.inverseInertia[column][row];
            }
        }
    }

    /// PHYSICS 3D QUERIES

    uint32_t physics3D_Raycast(
        float originX, float originY, float originZ, float directionX, float directionY, float directionZ,
        float maxDistance, Handle ignoredEntityHandle, Scripting::RaycastHit3DData *hit
    ) {
        *hit = {};
        World *world = GameContext::getCurrentWorld();
        Physics3D *physics = world ? world->findContext<Physics3D>() : nullptr;
        if (!physics) { return 0; }
        // 0 means "nothing is ignored"; a stale handle is reported like in any other entity call.
        Entity ignored = ignoredEntityHandle ? resolveScriptEntity(ignoredEntityHandle) : Entity();
        RaycastHit3D result;
        if (!physics->raycast(
                {originX, originY, originZ}, {directionX, directionY, directionZ}, maxDistance, ignored, result
            )) {
            return 0;
        }
        hit->entityHandle = result.entity.isValid() ? (Handle)result.entity.getId() : 0;
        hit->pointX = result.point.x;
        hit->pointY = result.point.y;
        hit->pointZ = result.point.z;
        hit->normalX = result.normal.x;
        hit->normalY = result.normal.y;
        hit->normalZ = result.normal.z;
        hit->distance = result.distance;
        hit->friction = result.friction;
        return 1;
    }

    /// WHEEL JOINT 3D COMPONENT — the script side speaks degrees, the physics — radians.

    void wheelJoint3DComponent_SetMotor(Handle entityHandle, uint32_t enabled, float speed, float maxTorque) {
        Entity entity;
        if (Physics3D *physics = physics3DOf(entityHandle, entity)) {
            physics->setWheelMotor(entity, enabled != 0, glm::radians(speed), maxTorque);
        }
    }

    void wheelJoint3DComponent_GetSpinSpeed(Handle entityHandle, float *speed) {
        Entity entity;
        *speed = 0.f;
        if (Physics3D *physics = physics3DOf(entityHandle, entity)) {
            *speed = glm::degrees(physics->getWheelSpinSpeed(entity));
        }
    }

    void wheelJoint3DComponent_SetSteeringAngle(Handle entityHandle, float angle) {
        Entity entity;
        if (Physics3D *physics = physics3DOf(entityHandle, entity)) {
            physics->setWheelSteeringAngle(entity, glm::radians(angle));
        }
    }

    void wheelJoint3DComponent_GetSteeringAngle(Handle entityHandle, float *angle) {
        Entity entity;
        *angle = 0.f;
        if (Physics3D *physics = physics3DOf(entityHandle, entity)) {
            *angle = glm::degrees(physics->getWheelSteeringAngle(entity));
        }
    }

    void wheelJoint3DComponent_SetSuspension(Handle entityHandle, float hertz, float dampingRatio) {
        Entity entity;
        if (Physics3D *physics = physics3DOf(entityHandle, entity)) {
            physics->setWheelSuspension(entity, hertz, dampingRatio);
        }
    }

    /// CHARACTER MOVER

    void character_Move(
        Handle entityHandle, float dx, float dy, float dz, float pushForce, float *velX, float *velY, float *velZ,
        float *outDx, float *outDy, float *outDz, uint32_t *outGrounded
    ) {
        Entity entity;
        Physics3D *physics = physics3DOf(entityHandle, entity);
        if (!physics) {
            *velX = 0.f;
            *velY = 0.f;
            *velZ = 0.f;
            *outDx = 0.f;
            *outDy = 0.f;
            *outDz = 0.f;
            *outGrounded = 0;
            return;
        }
        CharacterMoveResult result = physics->moveCharacter(entity, {dx, dy, dz}, {*velX, *velY, *velZ}, pushForce);
        *velX = result.velocity.x;
        *velY = result.velocity.y;
        *velZ = result.velocity.z;
        *outDx = result.translation.x;
        *outDy = result.translation.y;
        *outDz = result.translation.z;
        *outGrounded = result.grounded ? 1 : 0;
    }

    Scripting::PhysicsHostApi makeTable() {
        Scripting::PhysicsHostApi api;
        api.rigidbody2DComponent_ApplyForce = rigidbody2DComponent_ApplyForce;
        api.rigidbody2DComponent_ApplyLinearImpulse = rigidbody2DComponent_ApplyLinearImpulse;
        api.rigidbody2DComponent_GetLinearVelocity = rigidbody2DComponent_GetLinearVelocity;
        api.rigidbody2DComponent_SetLinearVelocity = rigidbody2DComponent_SetLinearVelocity;
        api.rigidbody2DComponent_GetMass = rigidbody2DComponent_GetMass;
        api.rigidbody2DComponent_GetFriction = rigidbody2DComponent_GetFriction;
        api.rigidbody2DComponent_SetFriction = rigidbody2DComponent_SetFriction;
        api.rigidbody3DComponent_ApplyForce = rigidbody3DComponent_ApplyForce;
        api.rigidbody3DComponent_ApplyLinearImpulse = rigidbody3DComponent_ApplyLinearImpulse;
        api.rigidbody3DComponent_GetLinearVelocity = rigidbody3DComponent_GetLinearVelocity;
        api.rigidbody3DComponent_SetLinearVelocity = rigidbody3DComponent_SetLinearVelocity;
        api.rigidbody3DComponent_GetMass = rigidbody3DComponent_GetMass;
        api.rigidbody3DComponent_ApplyForceAtPoint = rigidbody3DComponent_ApplyForceAtPoint;
        api.rigidbody3DComponent_ApplyLinearImpulseAtPoint = rigidbody3DComponent_ApplyLinearImpulseAtPoint;
        api.rigidbody3DComponent_ApplyTorque = rigidbody3DComponent_ApplyTorque;
        api.rigidbody3DComponent_ApplyAngularImpulse = rigidbody3DComponent_ApplyAngularImpulse;
        api.rigidbody3DComponent_GetAngularVelocity = rigidbody3DComponent_GetAngularVelocity;
        api.rigidbody3DComponent_SetAngularVelocity = rigidbody3DComponent_SetAngularVelocity;
        api.rigidbody3DComponent_GetPointVelocity = rigidbody3DComponent_GetPointVelocity;
        api.rigidbody3DComponent_GetMassData = rigidbody3DComponent_GetMassData;
        api.physics3D_Raycast = physics3D_Raycast;
        api.wheelJoint3DComponent_SetMotor = wheelJoint3DComponent_SetMotor;
        api.wheelJoint3DComponent_GetSpinSpeed = wheelJoint3DComponent_GetSpinSpeed;
        api.wheelJoint3DComponent_SetSteeringAngle = wheelJoint3DComponent_SetSteeringAngle;
        api.wheelJoint3DComponent_GetSteeringAngle = wheelJoint3DComponent_GetSteeringAngle;
        api.wheelJoint3DComponent_SetSuspension = wheelJoint3DComponent_SetSuspension;
        api.character_Move = character_Move;
        return api;
    }

    // The table lives as long as the module is registered (the registry keeps a pointer).
    const Scripting::PhysicsHostApi s_table = makeTable();

} // namespace

void registerScriptApi(ScriptHostApiRegistry &registry) {
    ScriptHostApiTable table;
    table.name = Scripting::PHYSICS_HOST_API_NAME;
    table.version = Scripting::PHYSICS_HOST_API_VERSION;
    table.table = &s_table;
    registry.add(std::move(table));
}

} // namespace Panda::Physics
