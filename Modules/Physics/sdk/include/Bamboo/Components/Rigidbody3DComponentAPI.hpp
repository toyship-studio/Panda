#pragma once

#include "Bamboo/Base.hpp"

namespace Bamboo {

// Dynamics of a Rigidbody3D body. Forces act over the next physics step, impulses change the
// velocity at once. Points are in world space; angular velocity is in radians per second around
// the vector. Calls on an entity without a body do nothing and return zeros.
namespace Rigidbody3DComponentAPI {
    // How a body answers a push, in world space as it stands now: an impulse J at a point p changes
    // the linear velocity by J / mass and the angular one by inverseInertia * cross(p - center, J).
    struct MassData {
        float mass = 0.f;             // kg; 0 — no body, or a body that forces do not move
        Vec3 center;                  // center of mass
        float inverseInertia[9] = {}; // inverse inertia tensor about the center of mass, by columns; zeros — the body does not turn
    };

    void applyForce(EntityHandle entity, Vec3 force);
    void applyLinearImpulse(EntityHandle entity, Vec3 impulse);
    // Off the center of mass a force or an impulse also turns the body.
    void applyForceAtPoint(EntityHandle entity, Vec3 force, Vec3 point);
    void applyLinearImpulseAtPoint(EntityHandle entity, Vec3 impulse, Vec3 point);
    void applyTorque(EntityHandle entity, Vec3 torque);
    void applyAngularImpulse(EntityHandle entity, Vec3 impulse);
    Vec3 getLinearVelocity(EntityHandle entity);
    void setLinearVelocity(EntityHandle entity, Vec3 velocity);
    Vec3 getAngularVelocity(EntityHandle entity);
    void setAngularVelocity(EntityHandle entity, Vec3 velocity);
    // Velocity of the point of the body that is at the given world point.
    Vec3 getPointVelocity(EntityHandle entity, Vec3 point);
    float getMass(EntityHandle entity);
    MassData getMassData(EntityHandle entity);
}; // namespace Rigidbody3DComponentAPI

} // namespace Bamboo
