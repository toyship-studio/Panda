#pragma once

#include "Bamboo/Base.hpp"

namespace Bamboo {

// Runtime drive of a Distance Joint 3D (a rope, a strut, a spring): the entity carries the joint
// component. Calls on an entity without the joint do nothing and return zeros.
namespace DistanceJoint3DComponentAPI {
    void setLength(EntityHandle entity, float length); // the rest length, m (a winch shortens it)
    // The motor pulls the rate of change of the length towards `speed` (m/s) with at most
    // `maxForce` (N); the joint must have its spring enabled.
    void setMotor(EntityHandle entity, bool enabled, float speed, float maxForce);
    float getCurrentLength(EntityHandle entity); // the distance between the anchors now, m
} // namespace DistanceJoint3DComponentAPI

} // namespace Bamboo
