#pragma once

#include "Bamboo/Base.hpp"

namespace Bamboo {

// Runtime drive of a Wheel Joint 3D. The entity is the WHEEL (it carries the joint component).
// Angles are in degrees and speeds in degrees per second, as in the joint component; a positive
// value is a right-handed rotation around the joint axis (spin) or the suspension axis (steering).
// Calls on an entity without the joint do nothing and return zeros.
namespace WheelJoint3DComponentAPI {
    // The motor pulls the spin speed towards `speed` with at most `maxTorque` (N·m): a target
    // speed with a torque drives the wheel, a zero speed with a torque brakes it. Disabled — the
    // wheel rolls freely.
    void setMotor(EntityHandle wheel, bool enabled, float speed, float maxTorque);
    float getSpinSpeed(EntityHandle wheel);
    // The joint spring turns the wheel to the target angle (the joint must have steering enabled).
    void setSteeringAngle(EntityHandle wheel, float angle);
    float getSteeringAngle(EntityHandle wheel); // the current angle, not the target
    // Spring frequency (Hz, over the reduced mass of the wheel and the chassis) and damping ratio.
    void setSuspension(EntityHandle wheel, float hertz, float dampingRatio);
}; // namespace WheelJoint3DComponentAPI

} // namespace Bamboo
