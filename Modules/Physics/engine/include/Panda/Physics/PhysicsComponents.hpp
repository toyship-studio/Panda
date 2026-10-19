#pragma once

#include <Panda/Base/Base.hpp>

#include <glm/glm.hpp>

#include <cstdint>

namespace Panda {

// The components of the physics module (context/physics.md). A body is a Rigidbody plus at least
// one collider on the same entity; joints connect the body of their entity with connectedEntity.
// The runtime data of a collider is the shape id of the solver.

struct Rigidbody2DComponent final {
    enum class BodyType : uint32_t { STATIC = 0, DYNAMIC, KINEMATIC };
    BodyType type = BodyType::STATIC;
    bool fixedRotation = false;
};

struct BoxCollider2DComponent final {
    glm::vec2 offset = {0.0f, 0.0f};
    glm::vec2 size = {0.5f, 0.5f};
    bool isSensor = false;

    float density = 1.0f;
    float friction = 0.5f;
    float restitution = 0.0f;

    // Storage for runtime
    uint8_t runtimeData[8];
};

struct Rigidbody3DComponent final {
    enum class BodyType : uint32_t { STATIC = 0, DYNAMIC, KINEMATIC };
    BodyType type = BodyType::STATIC;
    bool fixedRotation = false; // locks all three angular axes (b3MotionLocks.angular*)
};

struct BoxCollider3DComponent final {
    glm::vec3 offset = {0.0f, 0.0f, 0.0f};
    glm::vec3 size = {0.5f, 0.5f, 0.5f}; // half extents (as in 2D)
    bool isSensor = false;

    float density = 1.0f;
    float friction = 0.5f;
    float restitution = 0.0f;

    // Storage for runtime
    uint8_t runtimeData[8];
};

struct SphereCollider3DComponent final {
    glm::vec3 offset = {0.0f, 0.0f, 0.0f};
    float radius = 0.5f;
    bool isSensor = false;

    float density = 1.0f;
    float friction = 0.5f;
    float restitution = 0.0f;

    // Storage for runtime
    uint8_t runtimeData[8];
};

struct CapsuleCollider3DComponent final {
    glm::vec3 offset = {0.0f, 0.0f, 0.0f};
    float radius = 0.5f;
    float halfHeight = 0.5f; // from the center to the center of a hemisphere, Y axis
    bool isSensor = false;

    float density = 1.0f;
    float friction = 0.5f;
    float restitution = 0.0f;

    // Storage for runtime
    uint8_t runtimeData[8];
};

// --- Joints. Created at simulation start between this entity's body and connectedEntity;
// --- they capture the CURRENT relative pose of the bodies, limits are measured from it. Angles
// --- are in degrees.

// 2D hinge (pendulum, bridge, cart wheel): rotation in the plane around the anchor point.
struct RevoluteJoint2DComponent final {
    UUID connectedEntity = 0; // entity with the second Rigidbody2D; 0 — the joint is not created
    glm::vec2 anchor = {0.0f, 0.0f}; // joint point in this entity's local coordinates
    bool enableLimit = false;
    float lowerAngle = -45.0f;
    float upperAngle = 45.0f;
    bool enableMotor = false;
    float motorSpeed = 90.0f; // degrees/s
    float maxMotorTorque = 10.0f;
};

// 2D distance (suspension, cable): holds the distance between the anchors; with spring — a spring.
struct DistanceJoint2DComponent final {
    UUID connectedEntity = 0;
    glm::vec2 anchor = {0.0f, 0.0f};          // anchor on this body, local
    glm::vec2 connectedAnchor = {0.0f, 0.0f}; // anchor on the second body, local
    float length = 0.0f; // 0 — take the actual distance between the anchors at start
    bool enableSpring = false;
    float hertz = 4.0f;
    float dampingRatio = 0.5f;
};

// 3D hinge (door, knee): rotation around axis, which passes through anchor.
struct RevoluteJoint3DComponent final {
    UUID connectedEntity = 0;
    glm::vec3 anchor = {0.0f, 0.0f, 0.0f};
    glm::vec3 axis = {0.0f, 1.0f, 0.0f}; // rotation axis in this entity's local coordinates
    bool enableLimit = false;
    float lowerAngle = -45.0f;
    float upperAngle = 45.0f;
    bool enableMotor = false;
    float motorSpeed = 90.0f; // degrees/s
    float maxMotorTorque = 10.0f;
};

// 3D spherical (ragdoll: shoulder, hip, vertebra): a cone around axis + twist around it.
struct SphericalJoint3DComponent final {
    UUID connectedEntity = 0;
    glm::vec3 anchor = {0.0f, 0.0f, 0.0f};
    glm::vec3 axis = {0.0f, 0.0f, 1.0f}; // cone axis in this entity's local coordinates
    bool enableConeLimit = false;
    float coneAngle = 30.0f; // degrees [0..180]
    bool enableTwistLimit = false;
    float lowerTwistAngle = -30.0f;
    float upperTwistAngle = 30.0f;
};

// 3D wheel (a car wheel on a chassis). Unlike the other joints THIS entity is the wheel and
// connectedEntity is the chassis: every wheel carries its own joint. The wheel spins freely around
// axis, travels along suspensionAxis on a spring and, with steering, turns around suspensionAxis.
// Both axes are in this entity's local coordinates at simulation start; the suspension axis is
// then fixed in the chassis. The start pose is the rest position of the spring and the zero
// steering angle; suspension limits are meters from it (+ is compression). A positive steering
// angle and a positive motor speed are right-handed rotations around their axes. The body of the
// wheel is allowed to spin fast (box3d caps the angular speed of ordinary bodies).
struct WheelJoint3DComponent final {
    UUID connectedEntity = 0; // the chassis
    glm::vec3 anchor = {0.0f, 0.0f, 0.0f};
    glm::vec3 axis = {1.0f, 0.0f, 0.0f};           // spin axis (the axle)
    glm::vec3 suspensionAxis = {0.0f, 1.0f, 0.0f}; // suspension travel and steering axis
    bool enableSuspensionSpring = true;
    // Spring frequency over the REDUCED mass of the two bodies (close to the wheel mass under a
    // heavy chassis), not over the sprung mass: a car needs a value well above its ride frequency.
    float suspensionHertz = 5.0f;
    float suspensionDampingRatio = 0.7f;
    bool enableSuspensionLimit = true;
    float lowerSuspensionLimit = -0.1f;
    float upperSuspensionLimit = 0.1f;
    bool enableSteering = false;
    float steeringHertz = 10.0f; // spring that holds the steering angle set at runtime
    float steeringDampingRatio = 1.0f;
    float maxSteeringTorque = 1000.0f;
    bool enableSteeringLimit = true;
    float lowerSteeringAngle = -35.0f;
    float upperSteeringAngle = 35.0f;
    bool enableMotor = false;
    float motorSpeed = 0.0f; // degrees/s
    float maxMotorTorque = 100.0f;
};

} // namespace Panda
