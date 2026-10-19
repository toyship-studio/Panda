#pragma once

#include "Bamboo/Base.hpp"

namespace Bamboo {

namespace CharacterMoverAPI {
    struct MoveResult {
        Vec3 translation;      // actual displacement after collisions
        Vec3 velocity;         // input velocity clipped by contact planes
        bool grounded = false; // standing on a surface (slope up to ~45 degrees)
    };

    // Kinematic capsule character step: slides along obstacles, no tunneling.
    // Entity setup: Rigidbody3D (KINEMATIC) + Capsule Collider 3D.
    // translation — desired displacement for this frame (velocity * dt),
    // velocity — current velocity in m/s (returned clipped, e.g. zeroed against walls/ground),
    // pushForce — force in newtons applied to dynamic bodies the character presses into (0 = no pushing).
    MoveResult move(EntityHandle entity, Vec3 translation, Vec3 velocity, float pushForce = 0.f);
} // namespace CharacterMoverAPI

} // namespace Bamboo
