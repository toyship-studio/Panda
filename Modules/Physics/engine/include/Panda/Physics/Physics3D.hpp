#pragma once

#include "Panda/GameLogic/Entity.hpp"
#include "Panda/Physics/EntityBodies.hpp"
#include "Panda/Physics/PhysicsComponents.hpp"

#include <unordered_map>
#include <vector>

namespace Panda {

class World;

// Result of a kinematic character step (Physics3D::moveCharacter).
struct CharacterMoveResult {
    Vec3 translation;      // actual displacement (after sliding/blocking)
    Vec3 velocity;         // input velocity clipped by the contact planes
    bool grounded = false; // has support (a contact plane with an upward normal)
};

// Closest hit of a ray (Physics3D::raycast).
struct RaycastHit3D {
    Entity entity;        // owner of the hit body; invalid — the body has no entity
    Vec3 point;           // world point of the hit
    Vec3 normal;          // world normal of the surface at the point
    float distance = 0.f; // from the ray origin, m
    float friction = 0.f; // of the hit shape
};

// How a body answers a push, in world space as it stands now (Physics3D::getMassData): an impulse J
// at a point p changes the linear velocity by J / mass and the angular one by
// inverseInertia * cross(p - center, J).
struct MassData3D {
    float mass = 0.f;              // kg; 0 — no body, or a body that forces do not move
    Vec3 center;                   // center of mass
    glm::mat3 inverseInertia{0.f}; // inverse inertia tensor about the center of mass; zero — the body does not turn
};

// 3D physics on box3d — a mirror of Physics2D (the same life cycle: init at the world's start hook
// of the physics system, steps of a fixed length driven by the frame phases, the poses written into
// the transforms, contact/sensor events into scripts). Lives in World::context<Physics3D> while the
// world runs. Bodies are registered by Rigidbody3D + at least one collider (Box/Sphere/Capsule
// Collider3D).
class Physics3D {
    struct RuntimeBodyData {
        uint8_t data[8]; // b3BodyId
        // The pose before the last step: a frame that falls between two steps draws the body in
        // between (writeTransforms).
        glm::vec3 previousPosition{0.f};
        glm::quat previousRotation{1.f, 0.f, 0.f, 0.f};
        uint32_t writePass = 0; // the pass of writeTransforms that has written the body
    };
    struct RuntimeJointData {
        uint8_t data[8];
    };

public:
    Physics3D() = default;
    Physics3D(const Physics3D &) = delete;
    Physics3D &operator=(const Physics3D &) = delete;
    ~Physics3D() {
        shutdown();
    }
    void init(World *world);
    bool isInitialized();
    /// A step is beginStep → the scripts' fixedUpdate → step. beginStep remembers the poses the
    /// step starts from: what a script moves continuously in fixedUpdate (moveCharacter) is a
    /// part of the step's motion and is drawn between the two poses like the motion of the solver.
    void beginStep();
    /// The simulation of the step: the bodies move, the transforms get the new poses, then the
    /// contact and sensor events of the step go to the scripts.
    void step(World *world, double stepTime);
    /// Writes the body poses into the entity transforms. alpha — where between the pose before
    /// the last step (0) and the pose after it (1) the frame falls: a frame between two steps
    /// draws a body in between, one step behind, so that it moves smoothly at any frame rate.
    /// A body entity may stand anywhere in the hierarchy: the bodies are simulated in world space,
    /// and a pose is written relative to the parent of the entity, the parents first.
    void writeTransforms(World *world, float alpha);
    /// The set of physics components of the entity has changed (the body or a collider was added
    /// or removed): the body is created, removed or rebuilt from what the entity has now. A
    /// rebuilt body keeps its pose and velocities and loses its joints.
    void componentsUpdated(Entity entity);
    void propertiesUpdated(Entity entity);
    /// The entity leaves the simulation — it is destroyed, or deleted in the editor (the
    /// tombstone): its body goes, and so do the bodies the world systems created for it.
    void entityRemoved(Entity entity);
    /// A deleted entity is back (the deletion is undone): its body is created again, the world
    /// systems create theirs. The joints are not restored.
    void entityRestored(Entity entity);
    /// Teleports the body to the entity's transform without recreating it (gizmo drag at runtime;
    /// joints stay alive). A teleport is not a motion: the frame does not draw the way from the
    /// old place.
    void syncTransform(Entity entity);
    /// Applies force to center
    void applyForce(Entity entity, Vec3 force);
    /// Applies impulse to center. impulse = kg * m/s.
    void applyLinearImpulse(Entity entity, Vec3 impulse);
    void setLinearVelocity(Entity entity, Vec3 velocity);
    Vec3 getLinearVelocity(Entity entity);
    float getMass(Entity entity);
    MassData3D getMassData(Entity entity);
    /// Force / impulse at a world point: off the center of mass it also turns the body.
    void applyForceAtPoint(Entity entity, Vec3 force, Vec3 point);
    void applyLinearImpulseAtPoint(Entity entity, Vec3 impulse, Vec3 point);
    void applyTorque(Entity entity, Vec3 torque);
    void applyAngularImpulse(Entity entity, Vec3 impulse);
    /// Angular velocity — radians per second around the vector.
    void setAngularVelocity(Entity entity, Vec3 velocity);
    Vec3 getAngularVelocity(Entity entity);
    /// Velocity of the point of the body that is at the given world point.
    Vec3 getPointVelocity(Entity entity, Vec3 point);
    /// Closest solid shape along the ray; sensors are skipped. ignored — a body the ray passes
    /// through (usually the one that casts it); direction does not have to be normalized.
    bool raycast(Vec3 origin, Vec3 direction, float maxDistance, Entity ignored, RaycastHit3D &outHit);
    /// Wheel joint of the entity (WheelJoint3DComponent) at runtime; without the joint — no-ops
    /// and zeros. Angles are in radians, speeds — in radians per second. A changed target wakes
    /// the bodies, a driving motor keeps them awake.
    void setWheelMotor(Entity wheel, bool enabled, float speed, float maxTorque);
    float getWheelSpinSpeed(Entity wheel);
    void setWheelSteeringAngle(Entity wheel, float angle);
    float getWheelSteeringAngle(Entity wheel);
    void setWheelSuspension(Entity wheel, float hertz, float dampingRatio);
    /// Kinematic capsule character step (box3d character mover: cast → collide → solve planes).
    /// The entity must be registered with CapsuleCollider3D (body type — KINEMATIC). Called from
    /// fixedUpdate, the move is drawn smoothly between the steps; called once a frame, the
    /// character is drawn where it was put.
    /// translation — the desired displacement for the frame, velocity — the current velocity (for
    /// clipping against planes), pushForce — the force (N) applied to the dynamic bodies the
    /// character presses against (0 = does not push).
    CharacterMoveResult moveCharacter(Entity entity, Vec3 translation, Vec3 velocity, float pushForce);
    void shutdown();

    // For world systems (their WorldSystem::start after the physics: `after = {"physics"}`): the
    // box3d physics world as a number (Box3DHandles.hpp unpacks it) and registration of their own
    // body for an entity — contact events reach the scripts. The system owns such a body: it
    // destroys it and takes the registration back when the entity leaves the simulation
    // (WorldSystem::entityRemoved) and at stop.
    uint32_t worldHandle() const {
        return m_physicsWorldId;
    }
    void registerBody(uint32_t bodyIndex, Entity entity) {
        m_bodyEntityMap[bodyIndex] = entity;
    }
    void unregisterBody(uint32_t bodyIndex) {
        m_bodyEntityMap.erase(bodyIndex);
    }

private:
    bool canUse(Entity entity);
    bool isRegistered(Entity entity);
    void registerEntity(Entity entity);
    void removeEntity(Entity entity);
    // The pose of the entity's body into its transform, after the bodies above it in the
    // hierarchy: a pose is written relative to the parent as it stands at that moment.
    void writeTransform(Entity entity, float alpha);
    // Joints: created by the second pass of init (box3d itself destroys them with the bodies).
    void createJoints(World *world);
    // Body of a registered entity / live wheel joint of an entity; false — there is none.
    bool findBody(Entity entity, RuntimeBodyData &outBody);
    bool findWheelJoint(Entity entity, RuntimeJointData &outJoint);

    EntityBodies<RuntimeBodyData> m_bodies; // registerEntity and removeEntity are the only ones that fill it
    std::unordered_map<UUID, RuntimeJointData> m_wheelJointMap; // by the wheel entity
    std::unordered_map<uint32_t, Entity> m_bodyEntityMap;
    uint32_t m_physicsWorldId = 0;
    uint32_t m_writePass = 0; // counts the passes of writeTransforms
    bool m_stepping = false; // between beginStep and the end of step
    World *m_world = nullptr; // simulation world (init → shutdown)
};

} // namespace Panda
