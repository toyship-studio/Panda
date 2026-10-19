#pragma once

#include "Bamboo/Base.hpp"

namespace Bamboo {

// Queries against the 3D physics world.
namespace Physics3DAPI {
    struct RaycastHit {
        EntityHandle entity;  // owner of the hit body; invalid — the body has no entity
        Vec3 point;           // world point of the hit
        Vec3 normal;          // world normal of the surface at the point
        float distance = 0.f; // from the ray origin, m
        float friction = 0.f; // of the hit collider
    };

    // Closest solid shape along the ray; sensors are skipped. `direction` does not have to be
    // normalized. `ignored` — an entity whose body the ray passes through (usually the caster).
    // false — nothing within maxDistance.
    bool raycast(Vec3 origin, Vec3 direction, float maxDistance, RaycastHit &hit, EntityHandle ignored = {});
} // namespace Physics3DAPI

} // namespace Bamboo
