#pragma once

#include <Panda/Assets/Base/AssetRef.hpp>
#include <Panda/Base/Base.hpp>

#include <glm/glm.hpp>

#include <cstdint>

namespace Panda {

// The components of the physics module (context/physics.md). A body is a Rigidbody plus at least
// one collider on the same entity or on the entities under it that have no body of their own: such
// colliders join the body of the nearest entity above them (a compound body — a cabinet of boards,
// a table with legs — is one body with several shapes). Joints connect the body of their entity
// with connectedEntity. The runtime data of a collider is the shape id of the solver.

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

// A collider shaped by a mesh, in the local space of the entity (its scale applies; no offset).
// Convex: the convex hull of the mesh points, the shape a dynamic or kinematic body takes (the mass
// comes from its volume). Not convex: the triangles themselves, for static bodies only — box3d
// creates mesh contacts on static bodies alone, so on a moving body such a collider gives no shape
// and a warning. The mesh: an imported or a runtime mesh asset; empty — the mesh the entity itself
// renders (its ImportedSubmesh primitive, its RuntimeMesh or its PrimitiveMesh).
struct MeshCollider3DComponent final {
    AssetRef<Asset> mesh;
    bool convex = false;
    bool isSensor = false;

    float density = 1.0f;
    float friction = 0.5f;
    float restitution = 0.0f;

    // Storage for runtime
    uint8_t runtimeData[8] = {};

    MeshCollider3DComponent() = default;
    MeshCollider3DComponent(const MeshCollider3DComponent &other) = default;
    MeshCollider3DComponent &operator=(const MeshCollider3DComponent &other) = default;
};

// --- Joints. A joint lives while its component and both bodies do: it is created when the
// --- simulation starts or the component arrives, made again when a body is rebuilt, destroyed
// --- with the component. It captures the CURRENT relative pose of the bodies, limits are measured
// --- from it. Angles are in degrees. collideConnected — whether the two bodies still collide with
// --- each other (off by default: the parts of a mechanism overlap at the joint).

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
    bool collideConnected = false;
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
    bool collideConnected = false;
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
    bool collideConnected = false;
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
    bool collideConnected = false;
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
    bool collideConnected = false;
};

// 3D weld (a board glued to a cabinet, a part that breaks off): the two bodies keep the relative
// pose they have at start. Zero stiffness is rigid; a positive hertz makes a spring (a soft-body
// look). The reaction of the joint is readable from scripts (Physics3D::getJointReaction): a weld
// breaks when a script removes the component once the force exceeds what the material takes.
struct WeldJoint3DComponent final {
    UUID connectedEntity = 0;
    glm::vec3 anchor = {0.0f, 0.0f, 0.0f}; // the joint point in this entity's local coordinates
    float linearHertz = 0.0f;              // 0 — rigid
    float angularHertz = 0.0f;             // 0 — rigid
    float linearDampingRatio = 1.0f;
    float angularDampingRatio = 1.0f;
    bool collideConnected = false;
};

// 3D prismatic (a drawer, a piston, an elevator): the bodies slide along axis through anchor and
// do not turn relative to each other. The translation is zero at the start pose; the limits are
// meters from it; the spring drives to targetTranslation; the motor drives the speed.
struct PrismaticJoint3DComponent final {
    UUID connectedEntity = 0;
    glm::vec3 anchor = {0.0f, 0.0f, 0.0f};
    glm::vec3 axis = {1.0f, 0.0f, 0.0f}; // the slide axis in this entity's local coordinates
    bool enableSpring = false;
    float hertz = 2.0f;
    float dampingRatio = 0.5f;
    float targetTranslation = 0.0f;
    bool enableLimit = false;
    float lowerTranslation = -1.0f;
    float upperTranslation = 1.0f;
    bool enableMotor = false;
    float motorSpeed = 0.0f;     // m/s
    float maxMotorForce = 100.0f; // N
    bool collideConnected = false;
};

// 3D distance (a rope, a strut, a spring): holds the distance between an anchor on each body.
// Rigid unless the spring is on; with the spring the limit and the motor apply.
struct DistanceJoint3DComponent final {
    UUID connectedEntity = 0;
    glm::vec3 anchor = {0.0f, 0.0f, 0.0f};          // anchor on this body, local
    glm::vec3 connectedAnchor = {0.0f, 0.0f, 0.0f}; // anchor on the connected body, local
    float length = 0.0f; // 0 — the distance between the anchors at start
    bool enableSpring = false;
    float hertz = 4.0f;
    float dampingRatio = 0.5f;
    bool enableLimit = false;
    float minLength = 0.0f;
    float maxLength = 1.0f;
    bool enableMotor = false;
    float motorSpeed = 0.0f;     // m/s
    float maxMotorForce = 100.0f; // N
};

// 3D filter: the two bodies never collide with each other (a character and what it carries, the
// links of a chain two apart). Nothing else.
struct FilterJoint3DComponent final {
    UUID connectedEntity = 0;
};

// 3D motor: drives the relative velocity of the two bodies, and with a positive hertz a spring
// pulls them back to the relative pose they had at start (a kinematic-like drive on dynamic
// bodies, a conveyor, a magnet). Velocities are in world axes, the angular one in degrees per
// second. Set from scripts at runtime (MotorJoint3DComponentAPI).
struct MotorJoint3DComponent final {
    UUID connectedEntity = 0;
    glm::vec3 linearVelocity = {0.0f, 0.0f, 0.0f};  // m/s
    float maxVelocityForce = 1000.0f;               // N
    glm::vec3 angularVelocity = {0.0f, 0.0f, 0.0f}; // degrees/s
    float maxVelocityTorque = 1000.0f;              // N·m
    float linearHertz = 0.0f;                       // 0 — no position spring
    float linearDampingRatio = 1.0f;
    float maxSpringForce = 1000.0f;
    float angularHertz = 0.0f;
    float angularDampingRatio = 1.0f;
    float maxSpringTorque = 1000.0f;
    bool collideConnected = false;
};

} // namespace Panda
