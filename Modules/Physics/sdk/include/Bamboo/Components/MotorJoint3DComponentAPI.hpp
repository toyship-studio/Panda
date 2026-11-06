#pragma once

#include "Bamboo/Base.hpp"

namespace Bamboo {

// Runtime drive of a Motor Joint 3D: the entity carries the joint component; the joint drives the
// velocity of the connected body relative to the entity's (a conveyor, a magnet, a kinematic-like
// drive on dynamic bodies). Calls on an entity without the joint do nothing.
namespace MotorJoint3DComponentAPI {
    // The relative velocities the motor drives: linear in m/s, angular in degrees per second,
    // both in world axes.
    void setVelocities(EntityHandle entity, Vec3 linear, Vec3 angularDegreesPerSecond);
    // The force (N) and the torque (N·m) the motor may use for the velocities.
    void setLimits(EntityHandle entity, float maxForce, float maxTorque);
} // namespace MotorJoint3DComponentAPI

} // namespace Bamboo
