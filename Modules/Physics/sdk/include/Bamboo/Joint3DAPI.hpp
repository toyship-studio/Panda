#pragma once

#include "Bamboo/Base.hpp"

namespace Bamboo {

// The kinds of 3D joints of the Physics module, one component each: revoluteJoint3dComponent,
// sphericalJoint3dComponent, wheelJoint3dComponent, weldJoint3dComponent, prismaticJoint3dComponent,
// distanceJoint3dComponent, filterJoint3dComponent, motorJoint3dComponent. A joint sits on one
// entity and names the other in its component; it lives while the component and both bodies do.
enum class Joint3DKind : uint32_t { Revolute = 0, Spherical, Wheel, Weld, Prismatic, Distance, Filter, Motor };

// What a joint does to hold its bodies. Calls on an entity without a live joint of the kind return
// false and zeros.
namespace Joint3DAPI {
    struct Reaction {
        Vec3 force;  // N, world space
        Vec3 torque; // N·m, world space
    };

    // The force and the torque the joint applied during the last physics step. A breakable
    // connection: read the reaction of a weld every step and, once the force exceeds what the
    // material takes, remove the component (EntityAPI::removeComponent(entity,
    // "weldJoint3dComponent")) — the joint goes with it and the bodies part.
    bool getReaction(EntityHandle entity, Joint3DKind kind, Reaction &reaction);
} // namespace Joint3DAPI

} // namespace Bamboo
