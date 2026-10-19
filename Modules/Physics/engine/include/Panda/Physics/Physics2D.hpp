#pragma once

#include "Panda/GameLogic/Entity.hpp"
#include "Panda/Physics/EntityBodies.hpp"
#include "Panda/Physics/PhysicsComponents.hpp"

namespace Panda {

class World;

class Physics2D {
    struct RuntimeBodyData {
        uint8_t data[8]; // b2BodyId
        // The pose before the last step: a frame that falls between two steps draws the body in
        // between (writeTransforms).
        glm::vec2 previousPosition{0.f};
        float previousAngle = 0.f; // radians
        uint32_t writePass = 0;    // the pass of writeTransforms that has written the body
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
    /// The set of physics components of the entity has changed: the body is created or removed.
    void componentsUpdated(Entity entity);
    void propertiesUpdated(Entity entity);
    /// The entity leaves the simulation — it is destroyed, or deleted in the editor (the
    /// tombstone): its body goes with it.
    void entityRemoved(Entity entity);
    /// A deleted entity is back (the deletion is undone): its body is created again. The joints
    /// are not restored.
    void entityRestored(Entity entity);
    /// Teleports the body to the entity's transform without rebuilding shapes (position/rotation
    /// from outside: gizmo/script).
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
    // The pose of the entity's body into its transform, after the bodies above it in the
    // hierarchy: a pose is written relative to the parent as it stands at that moment.
    void writeTransform(Entity entity, float alpha);
    // Joints: created by the second pass of init (box2d itself destroys them with the bodies).
    void createJoints(World *world);
    // Body of a registered entity; false — there is none.
    bool findBody(Entity entity, RuntimeBodyData &outBody);

    EntityBodies<RuntimeBodyData> m_bodies; // registerEntity and removeEntity are the only ones that fill it
    std::unordered_map<uint32_t, Entity> m_bodyEntityMap;
    uint32_t m_physicsWorldId = 0;
    uint32_t m_writePass = 0; // counts the passes of writeTransforms
};

} // namespace Panda