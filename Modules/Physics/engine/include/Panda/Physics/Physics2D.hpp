#pragma once

#include "Panda/GameLogic/Entity.hpp"
#include "Panda/Physics/EntityBodies.hpp"
#include "Panda/Physics/PhysicsComponents.hpp"

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace Panda {

class World;

// The kinds of 2D joints, one component type each; an entity carries at most one joint of a kind.
enum class Joint2DKind : uint8_t { Revolute = 0, Distance, Count };

// 2D physics on box2d (context/physics.md). A body is an entity with Rigidbody2D and a box collider
// on it or on the entities under it that have no body of their own (a compound body).
class Physics2D {
    struct RuntimeBodyData {
        uint8_t data[8]; // b2BodyId
        // The pose before the last step: a frame that falls between two steps draws the body in
        // between (writeTransforms).
        glm::vec2 previousPosition{0.f};
        float previousAngle = 0.f; // radians
        uint32_t writePass = 0;    // the pass of writeTransforms that has written the body
    };
    struct RuntimeJointData {
        uint8_t data[8]; // b2JointId
    };
    // An entity whose collider went into the body of an entity above it, with its pose relative to
    // that body as the shape was placed (Physics3D::CompoundPart).
    struct CompoundPart {
        Entity entity;
        UUID body = 0;
        glm::vec2 position{0.f};
        float angle = 0.f;
        glm::vec2 scale{1.f};
    };

public:
    Physics2D() = default;
    Physics2D(const Physics2D &) = delete;
    Physics2D &operator=(const Physics2D &) = delete;
    ~Physics2D() {
        shutdown();
    }
    void init(World *world);
    bool isInitialized();
    /// A step is beginStep → the scripts' fixedUpdate → step. beginStep remembers the poses the
    /// step starts from.
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
    /// The set of physics components of the entity has changed: its body is created, removed or
    /// rebuilt from what it has now (with its motion), and so is the body above it whose shapes
    /// its collider is or was a part of. Joints are made again with a body.
    void componentsUpdated(Entity entity);
    /// A property edit or a scale change: the body (its own, or the one its collider is a part of)
    /// is made again at the entity's transform, at rest.
    void propertiesUpdated(Entity entity);
    /// A joint component of the entity was added, removed or edited: its joints are made again.
    void jointComponentsUpdated(Entity entity);
    /// The entity leaves the simulation — it is destroyed, or deleted in the editor (the
    /// tombstone): its body goes with it; a body it lent a collider to is made again without it.
    void entityRemoved(Entity entity);
    /// A deleted entity is back (the deletion is undone): its body is created again with its
    /// joints, a body it lends a collider to is made again with it.
    void entityRestored(Entity entity);
    /// Teleports the body to the entity's transform without rebuilding shapes (position/rotation
    /// from outside: gizmo/script). For a part of a compound body: the body is rebuilt around the
    /// moved part.
    void syncTransform(Entity entity);
    /// Applies force to center
    void applyForce(Entity entity, Vec2 force);
    /// Applies impulse to center. impulse = kg * m/s.
    void applyLinearImpulse(Entity entity, Vec2 impulse);
    void setLinearVelocity(Entity entity, Vec2 velocity);
    Vec2 getLinearVelocity(Entity entity);
    float getMass(Entity entity);
    float getFriction(Entity entity);
    void setFriction(Entity entity, float friction);
    void shutdown();

private:
    bool canUse(Entity entity);
    bool isRegistered(Entity entity);
    void registerEntity(Entity entity);
    void removeEntity(Entity entity);
    // The body is made again from the components as they are now (Physics3D::rebuildBody).
    void rebuildBody(Entity entity, bool keepMotion);
    void rebuildOwnersOf(Entity entity, bool keepMotion);
    // The box collider of `owner` as a shape of the body of `body`, placed by the owner's pose
    // relative to the body (the body itself: the origin, its own scale).
    void addShapeOf(
        Entity body, Entity owner, glm::vec2 relativePosition, float relativeAngle, glm::vec2 scale,
        RuntimeBodyData &runtimeData
    );
    bool hasShapesInSubtree(Entity entity);
    Entity bodyEntityAbove(Entity entity);
    void relativePose(Entity body, Entity entity, glm::vec2 &outPosition, float &outAngle, glm::vec2 &outScale);
    // The pose of the entity's body into its transform, after the bodies above it in the
    // hierarchy: a pose is written relative to the parent as it stands at that moment.
    void writeTransform(Entity entity, float alpha);
    // Joints: those of the entity from its components, and those of the other entities that name
    // it as the connected one, made again after its body was.
    void createJoints(World *world);
    void createJointsOf(Entity entity);
    bool createJoint(Entity entity, Joint2DKind kind);
    void destroyJoint(Entity entity, Joint2DKind kind);
    void destroyJointsOf(Entity entity);
    void recreateJointsTouching(Entity entity);
    // Body of a registered entity; false — there is none.
    bool findBody(Entity entity, RuntimeBodyData &outBody);

    EntityBodies<RuntimeBodyData> m_bodies; // registerEntity and removeEntity are the only ones that fill it
    std::unordered_map<UUID, CompoundPart> m_compoundParts; // by the collider entity; tracked in the world
    std::unordered_set<UUID> m_leaving; // the entities on their way out: a rebuild does not take their colliders
    std::unordered_map<uint64_t, RuntimeJointData> m_joints; // by (entity, kind)
    std::unordered_map<uint32_t, Entity> m_bodyEntityMap;
    uint32_t m_physicsWorldId = 0;
    uint32_t m_writePass = 0; // counts the passes of writeTransforms
    World *m_world = nullptr; // simulation world (init → shutdown)
};

} // namespace Panda