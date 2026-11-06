#pragma once

#include "Bamboo/Base.hpp"

namespace Bamboo {

// Runtime drive of a Prismatic Joint 3D (a drawer, a piston, an elevator): the entity carries the
// joint component. The translation is zero at the pose the joint was created in, positive along the
// authored axis. Calls on an entity without the joint do nothing and return zeros.
namespace PrismaticJoint3DComponentAPI {
    // The motor pulls the slide speed towards `speed` (m/s) with at most `maxForce` (N): a target
    // with a force drives, a zero target with a force holds, a disabled motor lets it slide.
    void setMotor(EntityHandle entity, bool enabled, float speed, float maxForce);
    // The translation the spring of the joint drives to (the joint must have its spring enabled).
    void setTarget(EntityHandle entity, float translation);
    float getTranslation(EntityHandle entity); // m, from the start pose
    float getSpeed(EntityHandle entity);       // m/s along the axis
} // namespace PrismaticJoint3DComponentAPI

} // namespace Bamboo
