#pragma once

#include "Panda/ScriptEngine/ABI/ScriptManifest.hpp"

#include <cstdint>

// The physics function table for scripts — a named contribution table
// (ScriptHostApi::hostApiTable_Find("physics", PHYSICS_HOST_API_VERSION)); the SDK side is
// Bamboo::Rigidbody2DComponentAPI, Rigidbody3DComponentAPI, MeshCollider3DComponentAPI,
// WheelJoint3DComponentAPI, Physics3DAPI and CharacterMoverAPI. Entities are handles of the current world; a call on an
// entity without a body or a joint does nothing and returns zeros. The contact and sensor events
// go the other way through the core plugin table (ScriptPluginApi::invokeBegin/EndCollisionTouch,
// invokeBegin/EndSensorOverlap).
namespace Panda::Scripting {

constexpr const char *PHYSICS_HOST_API_NAME = "physics";
// v2: the mesh collider; v3: the joints — weld, prismatic, distance, filter, motor, the reaction of
// a joint, collideConnected.
constexpr uint32_t PHYSICS_HOST_API_VERSION = 3;

// The kinds of 3D joints as the table numbers them (Panda::Joint3DKind on the engine side,
// Bamboo::Joint3DKind on the script side): revolute 0, spherical 1, wheel 2, weld 3, prismatic 4,
// distance 5, filter 6, motor 7.

using Rigidbody2DComponentApplyForceFn = void (*)(Handle entityHandle, float x, float y);
using Rigidbody2DComponentApplyLinearImpulseFn = void (*)(Handle entityHandle, float x, float y);
using Rigidbody2DComponentGetLinearVelocityFn = void (*)(Handle entityHandle, float *x, float *y);
using Rigidbody2DComponentSetLinearVelocityFn = void (*)(Handle entityHandle, float x, float y);
using Rigidbody2DComponentGetMassFn = void (*)(Handle entityHandle, float *mass);
using Rigidbody2DComponentGetFrictionFn = void (*)(Handle entityHandle, float *friction);
using Rigidbody2DComponentSetFrictionFn = void (*)(Handle entityHandle, float friction);

using Rigidbody3DComponentApplyForceFn = void (*)(Handle entityHandle, float x, float y, float z);
using Rigidbody3DComponentApplyLinearImpulseFn = void (*)(Handle entityHandle, float x, float y, float z);
using Rigidbody3DComponentGetLinearVelocityFn = void (*)(Handle entityHandle, float *x, float *y, float *z);
using Rigidbody3DComponentSetLinearVelocityFn = void (*)(Handle entityHandle, float x, float y, float z);
using Rigidbody3DComponentGetMassFn = void (*)(Handle entityHandle, float *mass);
// 3D body dynamics off the center of mass. Points are in world space, angular velocity is in
// radians per second around the vector.
using Rigidbody3DComponentApplyForceAtPointFn =
    void (*)(Handle entityHandle, float x, float y, float z, float pointX, float pointY, float pointZ);
using Rigidbody3DComponentApplyLinearImpulseAtPointFn =
    void (*)(Handle entityHandle, float x, float y, float z, float pointX, float pointY, float pointZ);
using Rigidbody3DComponentApplyTorqueFn = void (*)(Handle entityHandle, float x, float y, float z);
using Rigidbody3DComponentApplyAngularImpulseFn = void (*)(Handle entityHandle, float x, float y, float z);
using Rigidbody3DComponentGetAngularVelocityFn = void (*)(Handle entityHandle, float *x, float *y, float *z);
using Rigidbody3DComponentSetAngularVelocityFn = void (*)(Handle entityHandle, float x, float y, float z);
using Rigidbody3DComponentGetPointVelocityFn =
    void (*)(Handle entityHandle, float pointX, float pointY, float pointZ, float *x, float *y, float *z);
// How a 3D body answers a push, in world space as it stands now. center (3 values) — the center
// of mass; inverseInertia (9 values, by columns) — the inverse inertia tensor about it. A missing
// or immovable body gives zeros.
using Rigidbody3DComponentGetMassDataFn =
    void (*)(Handle entityHandle, float *mass, float *center, float *inverseInertia);

// Closest solid shape along a ray (sensors are skipped). ignoredEntityHandle — a body the ray
// passes through (0 — none). Returns 1 on a hit; entityHandle 0 — the hit body has no entity.
struct RaycastHit3DData final {
    Handle entityHandle;
    float pointX, pointY, pointZ;
    float normalX, normalY, normalZ;
    float distance;
    float friction;
};
using Physics3DRaycastFn = uint32_t (*)(
    float originX,
    float originY,
    float originZ,
    float directionX,
    float directionY,
    float directionZ,
    float maxDistance,
    Handle ignoredEntityHandle,
    RaycastHit3DData *hit
);

// Drive of a wheel joint (WheelJoint3DComponent on the wheel entity). Angles are in degrees and
// speeds in degrees per second, as in the joint components.
using WheelJoint3DComponentSetMotorFn = void (*)(Handle entityHandle, uint32_t enabled, float speed, float maxTorque);
using WheelJoint3DComponentGetSpinSpeedFn = void (*)(Handle entityHandle, float *speed);
using WheelJoint3DComponentSetSteeringAngleFn = void (*)(Handle entityHandle, float angle);
using WheelJoint3DComponentGetSteeringAngleFn = void (*)(Handle entityHandle, float *angle);
using WheelJoint3DComponentSetSuspensionFn = void (*)(Handle entityHandle, float hertz, float dampingRatio);

// Kinematic step of a capsule character: input — the desired displacement + pushForce (N, pushing
// dynamic bodies) + velocity (in-out, clipped against the contact planes); output — the actual
// displacement and the grounded flag.
using CharacterMoveFn = void (*)(Handle entityHandle, float dx, float dy, float dz, float pushForce,
                                 float *velX, float *velY, float *velZ, float *outDx, float *outDy,
                                 float *outDz, uint32_t *outGrounded);

// The mesh of a mesh collider (MeshCollider3DComponent). meshHandle — a runtime mesh of the game
// (0 — the mesh the entity renders); convex — the convex hull of the mesh, the shape of a moving
// body, otherwise the triangles themselves, static bodies only. The geometry is read at the call;
// while the kind of the shape stays, the body keeps its pose, velocities and joints.
using MeshCollider3DComponentSetMeshFn = void (*)(Handle entityHandle, Handle meshHandle, uint32_t convex);
using MeshCollider3DComponentGetMeshFn = void (*)(Handle entityHandle, Handle *meshHandle);

// What the joint of the kind on the entity applied during the last step to hold the bodies, in
// world space: force (3 values, N) and torque (3 values, N·m). Returns 1 when the joint lives.
using Joint3DGetReactionFn = uint32_t (*)(Handle entityHandle, uint32_t kind, float *force, float *torque);
// Prismatic joint (PrismaticJoint3DComponent): the motor (m/s, N), the spring target (m), the
// translation from the start pose and its speed.
using PrismaticJoint3DSetMotorFn = void (*)(Handle entityHandle, uint32_t enabled, float speed, float maxForce);
using PrismaticJoint3DSetTargetFn = void (*)(Handle entityHandle, float translation);
using PrismaticJoint3DGetTranslationFn = void (*)(Handle entityHandle, float *translation, float *speed);
// Distance joint (DistanceJoint3DComponent): the rest length, the motor (m/s, N), the current length.
using DistanceJoint3DSetLengthFn = void (*)(Handle entityHandle, float length);
using DistanceJoint3DSetMotorFn = void (*)(Handle entityHandle, uint32_t enabled, float speed, float maxForce);
using DistanceJoint3DGetCurrentLengthFn = void (*)(Handle entityHandle, float *length);
// Motor joint (MotorJoint3DComponent): the velocities it drives (m/s; degrees per second, world
// axes) and the force and torque it may use for them.
using MotorJoint3DSetVelocitiesFn =
    void (*)(Handle entityHandle, float linearX, float linearY, float linearZ, float angularX, float angularY, float angularZ);
using MotorJoint3DSetLimitsFn = void (*)(Handle entityHandle, float maxForce, float maxTorque);

struct PhysicsHostApi final {
    uint32_t version = PHYSICS_HOST_API_VERSION;
    Rigidbody2DComponentApplyForceFn rigidbody2DComponent_ApplyForce = nullptr;
    Rigidbody2DComponentApplyLinearImpulseFn rigidbody2DComponent_ApplyLinearImpulse = nullptr;
    Rigidbody2DComponentGetLinearVelocityFn rigidbody2DComponent_GetLinearVelocity = nullptr;
    Rigidbody2DComponentSetLinearVelocityFn rigidbody2DComponent_SetLinearVelocity = nullptr;
    Rigidbody2DComponentGetMassFn rigidbody2DComponent_GetMass = nullptr;
    Rigidbody2DComponentGetFrictionFn rigidbody2DComponent_GetFriction = nullptr;
    Rigidbody2DComponentSetFrictionFn rigidbody2DComponent_SetFriction = nullptr;
    Rigidbody3DComponentApplyForceFn rigidbody3DComponent_ApplyForce = nullptr;
    Rigidbody3DComponentApplyLinearImpulseFn rigidbody3DComponent_ApplyLinearImpulse = nullptr;
    Rigidbody3DComponentGetLinearVelocityFn rigidbody3DComponent_GetLinearVelocity = nullptr;
    Rigidbody3DComponentSetLinearVelocityFn rigidbody3DComponent_SetLinearVelocity = nullptr;
    Rigidbody3DComponentGetMassFn rigidbody3DComponent_GetMass = nullptr;
    Rigidbody3DComponentApplyForceAtPointFn rigidbody3DComponent_ApplyForceAtPoint = nullptr;
    Rigidbody3DComponentApplyLinearImpulseAtPointFn rigidbody3DComponent_ApplyLinearImpulseAtPoint = nullptr;
    Rigidbody3DComponentApplyTorqueFn rigidbody3DComponent_ApplyTorque = nullptr;
    Rigidbody3DComponentApplyAngularImpulseFn rigidbody3DComponent_ApplyAngularImpulse = nullptr;
    Rigidbody3DComponentGetAngularVelocityFn rigidbody3DComponent_GetAngularVelocity = nullptr;
    Rigidbody3DComponentSetAngularVelocityFn rigidbody3DComponent_SetAngularVelocity = nullptr;
    Rigidbody3DComponentGetPointVelocityFn rigidbody3DComponent_GetPointVelocity = nullptr;
    Rigidbody3DComponentGetMassDataFn rigidbody3DComponent_GetMassData = nullptr;
    Physics3DRaycastFn physics3D_Raycast = nullptr;
    WheelJoint3DComponentSetMotorFn wheelJoint3DComponent_SetMotor = nullptr;
    WheelJoint3DComponentGetSpinSpeedFn wheelJoint3DComponent_GetSpinSpeed = nullptr;
    WheelJoint3DComponentSetSteeringAngleFn wheelJoint3DComponent_SetSteeringAngle = nullptr;
    WheelJoint3DComponentGetSteeringAngleFn wheelJoint3DComponent_GetSteeringAngle = nullptr;
    WheelJoint3DComponentSetSuspensionFn wheelJoint3DComponent_SetSuspension = nullptr;
    CharacterMoveFn character_Move = nullptr;
    MeshCollider3DComponentSetMeshFn meshCollider3DComponent_SetMesh = nullptr;
    MeshCollider3DComponentGetMeshFn meshCollider3DComponent_GetMesh = nullptr;
    Joint3DGetReactionFn joint3D_GetReaction = nullptr;
    PrismaticJoint3DSetMotorFn prismaticJoint3D_SetMotor = nullptr;
    PrismaticJoint3DSetTargetFn prismaticJoint3D_SetTarget = nullptr;
    PrismaticJoint3DGetTranslationFn prismaticJoint3D_GetTranslation = nullptr;
    DistanceJoint3DSetLengthFn distanceJoint3D_SetLength = nullptr;
    DistanceJoint3DSetMotorFn distanceJoint3D_SetMotor = nullptr;
    DistanceJoint3DGetCurrentLengthFn distanceJoint3D_GetCurrentLength = nullptr;
    MotorJoint3DSetVelocitiesFn motorJoint3D_SetVelocities = nullptr;
    MotorJoint3DSetLimitsFn motorJoint3D_SetLimits = nullptr;
};

} // namespace Panda::Scripting
