#pragma once

#include "Base.hpp"

namespace Bamboo {

// A script on an entity. The frame calls, in this order: fixedUpdate before every physics step
// (zero, one or several times a frame, always with the same step — the place for forces,
// impulses and everything that drives the physics; the transforms hold the exact body poses),
// then update and lateUpdate once a frame with the frame time (the place for input, cameras and
// everything that is drawn; a body's transform is its pose interpolated for this frame).
class Script {
public:
    virtual ~Script() = default;

    virtual void start() {};
    virtual void fixedUpdate(float stepTime) {};
    virtual void update(float deltaTime) {};
    virtual void lateUpdate(float deltaTime) {};
    virtual void beginCollisionTouch(EntityHandle other) {};
    virtual void endCollisionTouch(EntityHandle other) {};
    virtual void beginSensorOverlap(EntityHandle sensor) {};
    virtual void endSensorOverlap(EntityHandle sensor) {};
    virtual void shutdown() {};
    EntityHandle getEntity() {
        return m_entity;
    }

private:
    EntityHandle m_entity;

    friend class ScriptRegistry;
};

} // namespace Bamboo
