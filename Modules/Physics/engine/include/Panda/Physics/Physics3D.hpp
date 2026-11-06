#pragma once

#include "Panda/GameLogic/Entity.hpp"
#include "Panda/Physics/EntityBodies.hpp"
#include "Panda/Physics/PhysicsComponents.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Panda {

class World;
struct MeshColliderCook; // the box3d geometry of a mesh collider (Physics3D.cpp)

// Result of a kinematic character step (Physics3D::moveCharacter).
struct CharacterMoveResult {
    Vec3 translation;      // actual displacement (after sliding/blocking)
    Vec3 velocity;         // input velocity clipped by the contact planes
    bool grounded = false; // has support (a contact plane with an upward normal)
};

// Closest hit of a ray (Physics3D::raycast).
struct RaycastHit3D {
    // The entity whose collider was hit (a part of a compound body — the part); invalid — a body
    // without an entity.
    Entity entity;
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

// The kinds of 3D joints, one component type each; an entity carries at most one joint of a kind.
// The numbering is the script ABI's (PhysicsScriptApi.hpp, Joint3DKind of the SDK).
enum class Joint3DKind : uint8_t { Revolute = 0, Spherical, Wheel, Weld, Prismatic, Distance, Filter, Motor, Count };

// What a joint applied to hold its bodies during the last step, in world space (a script breaks a
// weld when the force exceeds what the material takes).
struct JointReaction3D {
    Vec3 force;  // N
    Vec3 torque; // N·m
};

// 3D physics on box3d — a mirror of Physics2D (the same life cycle: init at the world's start hook
// of the physics system, steps of a fixed length driven by the frame phases, the poses written into
// the transforms, contact/sensor events into scripts). Lives in World::context<Physics3D> while the
// world runs. A body is an entity with Rigidbody3D and at least one collider on it or on the
// entities under it that have no body of their own (a compound body: the colliders of those
// entities join it, placed relative to it — context/physics.md, Bodies).
class Physics3D {
    struct RuntimeBodyData {
        uint8_t data[8]; // b3BodyId
        // The pose before the last step: a frame that falls between two steps draws the body in
        // between (writeTransforms).
        glm::vec3 previousPosition{0.f};
        glm::quat previousRotation{1.f, 0.f, 0.f, 0.f};
        uint32_t writePass = 0; // the pass of writeTransforms that has written the body
        // The geometry the mesh collider shapes of the body reference (a mesh shape holds a pointer
        // into it): the entity's own collider and those of the compound parts.
        std::shared_ptr<MeshColliderCook> ownMeshCook;
        std::vector<std::shared_ptr<MeshColliderCook>> partMeshCooks;
    };
    struct RuntimeJointData {
        uint8_t data[8]; // b3JointId
    };
    // An entity whose colliders went into the body of an entity above it, with its pose relative to
    // that body as the shapes were placed: a transform change that keeps it (the whole body moved)
    // costs nothing, one that does not (the part moved) rebuilds the body.
    struct CompoundPart {
        Entity entity;
        UUID body = 0;
        glm::vec3 position{0.f};
        glm::quat rotation{1.f, 0.f, 0.f, 0.f};
        glm::vec3 scale{1.f};
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
    /// and a pose is written relative to the parent of the entity, the parents first. The parts
    /// of a compound body follow their body through the hierarchy.
    void writeTransforms(World *world, float alpha);
    /// The set of physics components of the entity has changed (the body or a collider was added
    /// or removed): its body is created, removed or rebuilt from what the entity has now, and so
    /// is the body above it whose shapes the entity's colliders are or were a part of. A rebuilt
    /// body keeps its pose and velocities; its joints are made again.
    void componentsUpdated(Entity entity);
    /// A property of a body or a collider was edited, or the scale of the entity changed: the
    /// body (its own, or the one its collider is a part of) is made again at the entity's
    /// transform, at rest; its joints are made again.
    void propertiesUpdated(Entity entity);
    /// A joint component of the entity was added, removed or edited: the joints of the entity are
    /// made again from its components. The bodies keep their motion.
    void jointComponentsUpdated(Entity entity);
    /// The entity leaves the simulation — it is destroyed, or deleted in the editor (the
    /// tombstone): its body goes, and so do the bodies the world systems created for it; a body
    /// it lent a collider to is made again without it.
    void entityRemoved(Entity entity);
    /// A deleted entity is back (the deletion is undone): its body is created again with its
    /// joints, a body it lends a collider to is made again with it.
    void entityRestored(Entity entity);
    /// Teleports the body to the entity's transform without recreating it (gizmo drag at runtime;
    /// joints stay alive). A teleport is not a motion: the frame does not draw the way from the
    /// old place. For a part of a compound body: the body is rebuilt around the moved part (a
    /// part that kept its place relative to the body — the body itself moved — costs nothing).
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
    /// The reaction of the joint of the kind on the entity after the last step; false — no such
    /// live joint.
    bool getJointReaction(Entity entity, Joint3DKind kind, JointReaction3D &outReaction);
    /// Wheel joint of the entity (WheelJoint3DComponent) at runtime; without the joint — no-ops
    /// and zeros. Angles are in radians, speeds — in radians per second. A changed target wakes
    /// the bodies, a driving motor keeps them awake.
    void setWheelMotor(Entity wheel, bool enabled, float speed, float maxTorque);
    float getWheelSpinSpeed(Entity wheel);
    void setWheelSteeringAngle(Entity wheel, float angle);
    float getWheelSteeringAngle(Entity wheel);
    void setWheelSuspension(Entity wheel, float hertz, float dampingRatio);
    /// Prismatic joint (PrismaticJoint3DComponent) at runtime: the motor (m/s, N), the target of
    /// the spring (m, the spring must be on), the translation from the start pose and its speed.
    void setPrismaticMotor(Entity entity, bool enabled, float speed, float maxForce);
    void setPrismaticTarget(Entity entity, float translation);
    float getPrismaticTranslation(Entity entity);
    float getPrismaticSpeed(Entity entity);
    /// Distance joint (DistanceJoint3DComponent) at runtime: the rest length, the motor (m/s, N;
    /// the spring must be on), the current distance between the anchors.
    void setDistanceLength(Entity entity, float length);
    void setDistanceMotor(Entity entity, bool enabled, float speed, float maxForce);
    float getDistanceCurrentLength(Entity entity);
    /// Motor joint (MotorJoint3DComponent) at runtime: the velocities it drives (m/s and radians
    /// per second, world axes) and the force and torque it may use for them.
    void setMotorJointVelocities(Entity entity, Vec3 linear, Vec3 angular);
    void setMotorJointLimits(Entity entity, float maxForce, float maxTorque);
    /// The mesh collider of the entity gets a mesh (0 — the mesh the entity renders) and a kind of
    /// shape, from a script. The triangles are read now: a runtime mesh updated later needs the
    /// call again. While the kind of the shape stays, the shape changes in place and the body keeps
    /// its pose, velocities and joints; otherwise the body is rebuilt like on a property edit.
    void setMeshColliderMesh(Entity entity, AssetId mesh, bool convex);
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
    // The body is made again from the components as they are now; keepMotion — at the pose and
    // with the velocities it had, otherwise at the entity's transform, at rest. Its joints and the
    // joints that name it are made again. A body that cannot exist any more just goes.
    void rebuildBody(Entity entity, bool keepMotion);
    // The bodies the colliders of the entity are a part of — the one that holds them now and the
    // nearest one above the entity — are made again (keepMotion as in rebuildBody).
    void rebuildOwnersOf(Entity entity, bool keepMotion);
    // The colliders of `owner` as shapes of the body of `body`, placed by the pose of the owner
    // relative to the body (the body itself: the origin, its own scale).
    void addShapesOf(
        Entity body, Entity owner, const glm::vec3 &relativePosition, const glm::quat &relativeRotation,
        const glm::vec3 &scale, RuntimeBodyData &runtimeData
    );
    // Whether the entity, or an entity under it without a body of its own, carries a collider.
    bool hasShapesInSubtree(Entity entity);
    // The nearest entity above the entity with a registered body; an entity without a world — none.
    Entity bodyEntityAbove(Entity entity);
    // The pose of the entity relative to the body (box3d body frames carry no scale).
    void relativePose(Entity body, Entity entity, glm::vec3 &outPosition, glm::quat &outRotation, glm::vec3 &outScale);
    // The pose of the entity's body into its transform, after the bodies above it in the
    // hierarchy: a pose is written relative to the parent as it stands at that moment.
    void writeTransform(Entity entity, float alpha);
    // Joints: those of the entity from its components (both bodies must exist), and those of the
    // other entities that name the entity as the connected one.
    void createJoints(World *world);
    void createJointsOf(Entity entity);
    bool createJoint(Entity entity, Joint3DKind kind); // from the component of the kind; false — none, or a body is missing
    void destroyJoint(Entity entity, Joint3DKind kind);
    void destroyJointsOf(Entity entity);
    void recreateJointsTouching(Entity entity);
    // Body of a registered entity / live joint of a kind on an entity; false — there is none.
    bool findBody(Entity entity, RuntimeBodyData &outBody);
    bool findJoint(Entity entity, Joint3DKind kind, RuntimeJointData &outJoint);
    // The box3d geometry of the entity's mesh collider, built from the triangles of its source
    // once per simulation and shared by the bodies with the same source; nullptr — no triangles
    // (the reason is logged).
    std::shared_ptr<MeshColliderCook> cookMeshCollider(Entity entity, const MeshCollider3DComponent &collider);

    EntityBodies<RuntimeBodyData> m_bodies; // registerEntity and removeEntity are the only ones that fill it
    std::unordered_map<UUID, CompoundPart> m_compoundParts; // by the collider entity; tracked in the world
    std::unordered_set<UUID> m_leaving; // the entities on their way out of the world: a rebuild does not take their colliders
    // By (entity, kind): a joint lives while its component and both bodies do.
    std::unordered_map<uint64_t, RuntimeJointData> m_joints;
    std::unordered_map<uint32_t, Entity> m_bodyEntityMap;
    // The cooked mesh colliders by source (MeshColliderGeometry.hpp). A body keeps its own
    // reference: a cook dropped from here (the mesh was set again) lives on with the bodies built
    // from it.
    std::map<std::tuple<AssetId, uint32_t, uint32_t>, std::shared_ptr<MeshColliderCook>> m_meshCooks;
    uint32_t m_physicsWorldId = 0;
    uint32_t m_writePass = 0; // counts the passes of writeTransforms
    bool m_stepping = false; // between beginStep and the end of step
    World *m_world = nullptr; // simulation world (init → shutdown)
};

} // namespace Panda
