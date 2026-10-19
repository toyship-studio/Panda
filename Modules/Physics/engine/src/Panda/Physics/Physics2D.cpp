#include "Panda/Physics/Physics2D.hpp"
#include "Panda/GameLogic/World.hpp"

#include <box2d/box2d.h>
#include <glm/gtc/constants.hpp>

namespace Panda {

namespace {
    // The box2d identifier from its opaque storage in the Physics2D table (the engine headers do
    // not pull in box2d).
    template<typename TData>
    b2BodyId bodyIdOf(const TData &runtimeData) {
        b2BodyId bodyId;
        memcpy(&bodyId, &runtimeData.data, sizeof(b2BodyId));
        return bodyId;
    }
} // namespace

inline b2BodyType rigidbody2DTypeToBox2DBody(Rigidbody2DComponent::BodyType bodyType) {
    switch (bodyType) {
        case Rigidbody2DComponent::BodyType::STATIC:
            return b2_staticBody;
        case Rigidbody2DComponent::BodyType::DYNAMIC:
            return b2_dynamicBody;
        case Rigidbody2DComponent::BodyType::KINEMATIC:
            return b2_kinematicBody;
    }
    PND_ASSERT(false, "Unknown body type");
    return b2_staticBody;
}

inline Rigidbody2DComponent::BodyType rigidbody2DTypeFromBox2DBody(b2BodyType bodyType) {
    switch (bodyType) {
        case b2_staticBody:
            return Rigidbody2DComponent::BodyType::STATIC;
        case b2_dynamicBody:
            return Rigidbody2DComponent::BodyType::DYNAMIC;
        case b2_kinematicBody:
            return Rigidbody2DComponent::BodyType::KINEMATIC;
        case b2_bodyTypeCount: {
            PND_ASSERT(false, "Unknown body type");
            break;
        }
    }
    PND_ASSERT(false, "Unknown body type");
    return Rigidbody2DComponent::BodyType::STATIC;
}

uint32_t b2WorldIdToInt(b2WorldId id) {
    return ((uint32_t)id.index1 << 16) | (uint32_t)id.revision;
}

b2WorldId IntToB2WorldId(uint32_t val) {
    b2WorldId id;
    id.index1 = (uint16_t)(val >> 16);
    id.revision = (uint16_t)(val & 0xFFFF);
    return id;
}

void Physics2D::init(World *world) {
    PND_STATIC_ASSERT(sizeof(b2BodyId) <= sizeof(RuntimeBodyData::data), "Rigidbody2D runtime body wrong data size");
    PND_STATIC_ASSERT(
        sizeof(b2ShapeId) <= sizeof(BoxCollider2DComponent::runtimeData), "Rigidbody2D runtime body wrong data size"
    );
    b2WorldDef worldDef = b2DefaultWorldDef();
    worldDef.restitutionThreshold = 0.5;
    b2WorldId worldId = b2CreateWorld(&worldDef);
    m_physicsWorldId = b2WorldIdToInt(worldId);
    // Register all entities
    auto view = world->getAllEntitiesWith<Rigidbody2DComponent>();
    for (auto entityHandle : view) {
        if (!world->isValidEntt(entityHandle)) { continue; }
        Entity entity = world->entity(entityHandle);
        registerEntity(entity);
    }
    // Joints — in a second pass: both bodies must exist by now.
    createJoints(world);
}

static glm::vec2 rotate2D(glm::vec2 v, float radians) {
    const float c = cos(radians);
    const float s = sin(radians);
    return {c * v.x - s * v.y, s * v.x + c * v.y};
}

void Physics2D::createJoints(World *world) {
    b2WorldId worldId = IntToB2WorldId(m_physicsWorldId);
    // Both bodies must exist; an unconnected joint (connectedEntity=0) is silently skipped.
    auto findJointBodies = [this, world](
                               Entity entity, UUID connectedId, b2BodyId &outBodyA, b2BodyId &outBodyB,
                               Entity &outConnected
                           ) {
        if (!connectedId) { return false; }
        const RuntimeBodyData *body = m_bodies.find(entity.getId());
        if (body == nullptr) { return false; }
        outConnected = world->getById(connectedId);
        const RuntimeBodyData *connectedBody = outConnected.isValid() ? m_bodies.find(connectedId) : nullptr;
        if (connectedBody == nullptr) {
            LOG_ERROR_EDITOR("Joint on '%s': connected entity has no Rigidbody2D", entity.getName().c_str());
            return false;
        }
        outBodyA = bodyIdOf(*body);
        outBodyB = bodyIdOf(*connectedBody);
        return true;
    };
    // Authoring model: the anchors are in the local space of their bodies; the frame/reference
    // angle fix the current pose.
    auto revoluteView = world->getAllEntitiesWith<RevoluteJoint2DComponent>();
    for (auto entityHandle : revoluteView) {
        if (!world->isValidEntt(entityHandle)) { continue; }
        Entity entity = world->entity(entityHandle);
        auto &joint = entity.getComponent<RevoluteJoint2DComponent>();
        b2BodyId bodyA, bodyB;
        Entity connected;
        if (!findJointBodies(entity, joint.connectedEntity, bodyA, bodyB, connected)) { continue; }
        auto ownerTransform = entity.calculateWorldSpaceTransform();
        auto connectedTransform = connected.calculateWorldSpaceTransform();
        const float ownerAngle = glm::radians(ownerTransform.getRotationEuler().z);
        const float connectedAngle = glm::radians(connectedTransform.getRotationEuler().z);
        const glm::vec2 ownerScale = glm::abs(glm::vec2(ownerTransform.getScale()));
        const glm::vec2 anchorA = joint.anchor * ownerScale;
        const glm::vec2 ownerPosition = glm::vec2(ownerTransform.getPosition());
        const glm::vec2 connectedPosition = glm::vec2(connectedTransform.getPosition());
        const glm::vec2 worldAnchor = ownerPosition + rotate2D(anchorA, ownerAngle);
        const glm::vec2 anchorB = rotate2D(worldAnchor - connectedPosition, -connectedAngle);
        b2RevoluteJointDef def = b2DefaultRevoluteJointDef();
        def.bodyIdA = bodyA;
        def.bodyIdB = bodyB;
        def.localAnchorA = {anchorA.x, anchorA.y};
        def.localAnchorB = {anchorB.x, anchorB.y};
        def.referenceAngle = connectedAngle - ownerAngle;
        def.enableLimit = joint.enableLimit;
        def.lowerAngle = glm::radians(joint.lowerAngle);
        def.upperAngle = glm::radians(joint.upperAngle);
        def.enableMotor = joint.enableMotor;
        def.motorSpeed = glm::radians(joint.motorSpeed);
        def.maxMotorTorque = joint.maxMotorTorque;
        b2CreateRevoluteJoint(worldId, &def);
    }
    auto distanceView = world->getAllEntitiesWith<DistanceJoint2DComponent>();
    for (auto entityHandle : distanceView) {
        if (!world->isValidEntt(entityHandle)) { continue; }
        Entity entity = world->entity(entityHandle);
        auto &joint = entity.getComponent<DistanceJoint2DComponent>();
        b2BodyId bodyA, bodyB;
        Entity connected;
        if (!findJointBodies(entity, joint.connectedEntity, bodyA, bodyB, connected)) { continue; }
        auto ownerTransform = entity.calculateWorldSpaceTransform();
        auto connectedTransform = connected.calculateWorldSpaceTransform();
        const float ownerAngle = glm::radians(ownerTransform.getRotationEuler().z);
        const float connectedAngle = glm::radians(connectedTransform.getRotationEuler().z);
        const glm::vec2 anchorA = joint.anchor * glm::abs(glm::vec2(ownerTransform.getScale()));
        const glm::vec2 anchorB = joint.connectedAnchor * glm::abs(glm::vec2(connectedTransform.getScale()));
        const glm::vec2 worldAnchorA = glm::vec2(ownerTransform.getPosition()) + rotate2D(anchorA, ownerAngle);
        const glm::vec2 worldAnchorB =
            glm::vec2(connectedTransform.getPosition()) + rotate2D(anchorB, connectedAngle);
        b2DistanceJointDef def = b2DefaultDistanceJointDef();
        def.bodyIdA = bodyA;
        def.bodyIdB = bodyB;
        def.localAnchorA = {anchorA.x, anchorA.y};
        def.localAnchorB = {anchorB.x, anchorB.y};
        def.length = joint.length > 0.f ? joint.length : glm::distance(worldAnchorA, worldAnchorB);
        def.enableSpring = joint.enableSpring;
        def.hertz = joint.hertz;
        def.dampingRatio = joint.dampingRatio;
        b2CreateDistanceJoint(worldId, &def);
    }
}

bool Physics2D::isInitialized() {
    return m_physicsWorldId;
}

void Physics2D::beginStep() {
    m_bodies.forEach([](RuntimeBodyData &body) {
        const b2BodyId bodyId = bodyIdOf(body);
        const b2Vec2 position = b2Body_GetPosition(bodyId);
        body.previousPosition = {position.x, position.y};
        body.previousAngle = b2Rot_GetAngle(b2Body_GetRotation(bodyId));
    });
}

void Physics2D::step(World *world, double stepTime) {
    const int32_t subStepCount = 4;
    b2WorldId worldId = IntToB2WorldId(m_physicsWorldId);
    b2World_Step(worldId, stepTime, subStepCount);
    writeTransforms(world, 1.f);
    //----------------------------------//
    //          CONTACT EVENTS          //
    //----------------------------------//
    b2ContactEvents contactEvents = b2World_GetContactEvents(worldId);
    for (int i = 0; i < contactEvents.endCount; ++i) {
        b2ContactEndTouchEvent event = contactEvents.endEvents[i];
        if (!b2Shape_IsValid(event.shapeIdA) || !b2Shape_IsValid(event.shapeIdB)) { continue; }
        b2BodyId bodyIdA = b2Shape_GetBody(event.shapeIdA);
        b2BodyId bodyIdB = b2Shape_GetBody(event.shapeIdB);
        Entity entityA = m_bodyEntityMap.at(bodyIdA.index1);
        Entity entityB = m_bodyEntityMap.at(bodyIdB.index1);
        if (!entityA.isValid() || !entityB.isValid()) { continue; }
        ScriptListComponent &scriptListA = entityA.getComponent<ScriptListComponent>();
        for (ExternalScript &script : scriptListA.scripts) {
            script.invokeEndCollisionTouch(entityB.getId());
        }
        ScriptListComponent &scriptListB = entityB.getComponent<ScriptListComponent>();
        for (ExternalScript &script : scriptListB.scripts) {
            script.invokeEndCollisionTouch(entityA.getId());
        }
    }
    for (int i = 0; i < contactEvents.beginCount; ++i) {
        b2ContactBeginTouchEvent event = contactEvents.beginEvents[i];
        if (!b2Shape_IsValid(event.shapeIdA) || !b2Shape_IsValid(event.shapeIdB)) { continue; }
        b2BodyId bodyIdA = b2Shape_GetBody(event.shapeIdA);
        b2BodyId bodyIdB = b2Shape_GetBody(event.shapeIdB);
        Entity entityA = m_bodyEntityMap.at(bodyIdA.index1);
        Entity entityB = m_bodyEntityMap.at(bodyIdB.index1);
        if (!entityA.isValid() || !entityB.isValid()) { continue; }
        ScriptListComponent &scriptListA = entityA.getComponent<ScriptListComponent>();
        for (ExternalScript &script : scriptListA.scripts) {
            script.invokeBeginCollisionTouch(entityB.getId());
        }
        ScriptListComponent &scriptListB = entityB.getComponent<ScriptListComponent>();
        for (ExternalScript &script : scriptListB.scripts) {
            script.invokeBeginCollisionTouch(entityA.getId());
        }
    }
    //----------------------------------//
    //          SENSOR EVENTS           //
    //----------------------------------//
    b2SensorEvents sensorEvents = b2World_GetSensorEvents(worldId);
    for (int i = 0; i < sensorEvents.beginCount; ++i) {
        b2SensorBeginTouchEvent event = sensorEvents.beginEvents[i];
        if (!b2Shape_IsValid(event.sensorShapeId) || !b2Shape_IsValid(event.visitorShapeId)) { continue; }
        b2BodyId bodyIdSensor = b2Shape_GetBody(event.sensorShapeId);
        b2BodyId bodyIdVisitor = b2Shape_GetBody(event.visitorShapeId);
        Entity entitySensor = m_bodyEntityMap.at(bodyIdSensor.index1);
        Entity entityVisitor = m_bodyEntityMap.at(bodyIdVisitor.index1);
        if (!entitySensor.isValid() || !entityVisitor.isValid()) { continue; }
        ScriptListComponent &scriptListVisitor = entityVisitor.getComponent<ScriptListComponent>();
        for (ExternalScript &script : scriptListVisitor.scripts) {
            script.invokeBeginSensorOverlap(entitySensor.getId());
        }
    }
    for (int i = 0; i < sensorEvents.endCount; ++i) {
        b2SensorEndTouchEvent event = sensorEvents.endEvents[i];
        if (!b2Shape_IsValid(event.sensorShapeId) || !b2Shape_IsValid(event.visitorShapeId)) { continue; }
        b2BodyId bodyIdSensor = b2Shape_GetBody(event.sensorShapeId);
        b2BodyId bodyIdVisitor = b2Shape_GetBody(event.visitorShapeId);
        Entity entitySensor = m_bodyEntityMap.at(bodyIdSensor.index1);
        Entity entityVisitor = m_bodyEntityMap.at(bodyIdVisitor.index1);
        if (!entitySensor.isValid() || !entityVisitor.isValid()) { continue; }
        ScriptListComponent &scriptListVisitor = entityVisitor.getComponent<ScriptListComponent>();
        for (ExternalScript &script : scriptListVisitor.scripts) {
            script.invokeEndSensorOverlap(entitySensor.getId());
        }
    }
}

void Physics2D::writeTransforms(World *world, float alpha) {
    m_writePass++;
    auto view = world->getAllEntitiesWith<Rigidbody2DComponent>();
    for (auto entityHandle : view) {
        if (!world->isValidEntt(entityHandle)) { continue; }
        writeTransform(world->entity(entityHandle), alpha);
    }
}

void Physics2D::writeTransform(Entity entity, float alpha) {
    RuntimeBodyData *body = m_bodies.find(entity.getId());
    if (body != nullptr && body->writePass == m_writePass) { return; }
    // Everything above the entity first; an entity without a body only passes the call up.
    Entity parent = entity.getParent();
    if (parent.isValid()) { writeTransform(parent, alpha); }
    if (body == nullptr) { return; }
    body->writePass = m_writePass;
    const b2BodyId bodyId = bodyIdOf(*body);
    const b2Vec2 stepped = b2Body_GetPosition(bodyId);
    glm::vec2 position(stepped.x, stepped.y);
    float angle = b2Rot_GetAngle(b2Body_GetRotation(bodyId));
    if (alpha < 1.f) {
        position = glm::mix(body->previousPosition, position, alpha);
        // The shorter way around between the two angles.
        const float turn = glm::mod(angle - body->previousAngle + glm::pi<float>(), glm::two_pi<float>()) - glm::pi<float>();
        angle = body->previousAngle + turn * alpha;
    }
    entity.getWorld()->setWorldPose(entity, {position.x, position.y, 0.f}, glm::quat(glm::vec3(0.f, 0.f, angle)));
}

void Physics2D::registerEntity(Entity entity) {
    if (!isInitialized()) { return; }
    b2WorldId worldId = IntToB2WorldId(m_physicsWorldId);
    auto transform = entity.calculateWorldSpaceTransform();
    auto &rb2d = entity.getComponent<Rigidbody2DComponent>();
    float rotationDeg = transform.getRotationEuler().z;
    float rotationRad = glm::radians(rotationDeg);
    b2BodyDef bodyDef = b2DefaultBodyDef();
    bodyDef.type = rigidbody2DTypeToBox2DBody(rb2d.type);
    bodyDef.position = {transform.getPosition().x, transform.getPosition().y};
    bodyDef.rotation = {cos(rotationRad), sin(rotationRad)};
    bodyDef.fixedRotation = rb2d.fixedRotation;
    b2BodyId bodyId = b2CreateBody(worldId, &bodyDef);
    RuntimeBodyData runtimeData;
    memcpy(&runtimeData.data, &bodyId, sizeof(b2BodyId));
    runtimeData.previousPosition = {transform.getPosition().x, transform.getPosition().y};
    runtimeData.previousAngle = rotationRad;
    if (entity.hasComponent<BoxCollider2DComponent>()) {
        auto &bc2d = entity.getComponent<BoxCollider2DComponent>();
        b2Polygon boxShape = b2MakeOffsetBox(
            abs(bc2d.size.x * transform.getScale().x),
            abs(bc2d.size.y * transform.getScale().y),
            {bc2d.offset.x, bc2d.offset.y},
            {1.0f, 0.0f}
        );
        b2ShapeDef shapeDef = b2DefaultShapeDef();
        shapeDef.density = bc2d.density;
        shapeDef.friction = bc2d.friction;
        shapeDef.restitution = bc2d.restitution;
        shapeDef.isSensor = bc2d.isSensor;
        b2ShapeId shapeId = b2CreatePolygonShape(bodyId, &shapeDef, &boxShape);
        memcpy(&bc2d.runtimeData, &shapeId, sizeof(b2ShapeId));
    }
    m_bodyEntityMap[bodyId.index1] = entity;
    m_bodies.add(entity, runtimeData);
}

void Physics2D::componentsUpdated(Entity entity) {
    if (!isInitialized()) { return; }
    if (!canUse(entity) && isRegistered(entity)) {
        removeEntity(entity);
        return;
    }
    if (canUse(entity) && !isRegistered(entity)) {
        registerEntity(entity);
        return;
    }
}

void Physics2D::entityRemoved(Entity entity) {
    if (isInitialized() && isRegistered(entity)) { removeEntity(entity); }
}

void Physics2D::entityRestored(Entity entity) {
    if (isInitialized() && canUse(entity) && !isRegistered(entity)) { registerEntity(entity); }
}

void Physics2D::syncTransform(Panda::Entity entity) {
    // Body teleport (position/rotation changed from outside: gizmo/script/inspector). Without
    // rebuilding the shapes — cheap for a per-frame drag. The velocities are zeroed ("put it down —
    // it stays", like propertiesUpdated), wake — a teleported body must recompute its contacts.
    if (!isInitialized()) { return; }
    RuntimeBodyData *registered = m_bodies.find(entity.getId());
    if (registered == nullptr) { return; }
    RuntimeBodyData &runtimeData = *registered;
    const b2BodyId bodyId = bodyIdOf(runtimeData);
    auto transform = entity.calculateWorldSpaceTransform();
    const float rotationRad = glm::radians(transform.getRotationEuler().z);
    b2Body_SetTransform(
        bodyId, {transform.getPosition().x, transform.getPosition().y}, {cos(rotationRad), sin(rotationRad)}
    );
    b2Body_SetLinearVelocity(bodyId, {0, 0});
    b2Body_SetAngularVelocity(bodyId, 0);
    b2Body_SetAwake(bodyId, true);
    // A teleport is not a motion: the frame does not draw the way from the old place.
    runtimeData.previousPosition = {transform.getPosition().x, transform.getPosition().y};
    runtimeData.previousAngle = rotationRad;
}

void Physics2D::propertiesUpdated(Panda::Entity entity) {
    if (!isInitialized()) { return; }
    RuntimeBodyData *registered = m_bodies.find(entity.getId());
    if (registered == nullptr) { return; }
    RuntimeBodyData &runtimeData = *registered;
    const b2BodyId bodyId = bodyIdOf(runtimeData);

    auto transform = entity.calculateWorldSpaceTransform();
    float rotationDeg = transform.getRotationEuler().z;
    float rotationRad = glm::radians(rotationDeg);
    auto &rb2d = entity.getComponent<Rigidbody2DComponent>();
    b2Body_SetType(bodyId, rigidbody2DTypeToBox2DBody(rb2d.type));
    b2Body_SetTransform(
        bodyId, {transform.getPosition().x, transform.getPosition().y}, {cos(rotationRad), sin(rotationRad)}
    );
    b2Body_SetFixedRotation(bodyId, rb2d.fixedRotation);

    if (entity.hasComponent<BoxCollider2DComponent>()) {
        auto &bc2d = entity.getComponent<BoxCollider2DComponent>();
        b2ShapeId shapeId;
        memcpy(&shapeId, &bc2d.runtimeData, sizeof(b2ShapeId));

        b2Polygon boxShape = b2MakeOffsetBox(
            abs(bc2d.size.x * transform.getScale().x),
            abs(bc2d.size.y * transform.getScale().y),
            {bc2d.offset.x, bc2d.offset.y},
            {1.0f, 0.0f}
        );
        b2Shape_SetPolygon(shapeId, &boxShape);
        b2Shape_SetDensity(shapeId, bc2d.density, true);
        b2Shape_SetFriction(shapeId, bc2d.friction);
        b2Shape_SetRestitution(shapeId, bc2d.restitution);
        PND_ASSERT(b2Shape_IsSensor(shapeId) == bc2d.isSensor, "CHANGING IS SENSOR IN RUNTIME IS NOT SUPPORTED");
    }
    b2Body_SetLinearVelocity(bodyId, {0, 0});
    b2Body_SetAngularVelocity(bodyId, 0);
    runtimeData.previousPosition = {transform.getPosition().x, transform.getPosition().y};
    runtimeData.previousAngle = rotationRad;
}

void Physics2D::removeEntity(Entity entity) {
    if (!isInitialized()) { return; }
    const RuntimeBodyData *registered = m_bodies.find(entity.getId());
    if (registered == nullptr) { return; }
    const b2BodyId bodyId = bodyIdOf(*registered);
    b2DestroyBody(bodyId);
    m_bodyEntityMap.erase(bodyId.index1);
    m_bodies.remove(entity);
}

bool Physics2D::findBody(Entity entity, RuntimeBodyData &outBody) {
    if (!isInitialized()) { return false; }
    const RuntimeBodyData *registered = m_bodies.find(entity.getId());
    if (registered == nullptr) { return false; }
    outBody = *registered;
    return true;
}

// An entity without a body gives no-ops and zeros.
void Physics2D::applyForce(Entity entity, Vec2 force) {
    RuntimeBodyData body;
    if (!findBody(entity, body)) { return; }
    b2Body_ApplyForceToCenter(bodyIdOf(body), {force.x, force.y}, true);
}

void Physics2D::applyLinearImpulse(Entity entity, Vec2 impulse) {
    RuntimeBodyData body;
    if (!findBody(entity, body)) { return; }
    b2Body_ApplyLinearImpulseToCenter(bodyIdOf(body), {impulse.x, impulse.y}, true);
}

void Physics2D::setLinearVelocity(Entity entity, Vec2 velocity) {
    RuntimeBodyData body;
    if (!findBody(entity, body)) { return; }
    b2Body_SetLinearVelocity(bodyIdOf(body), {velocity.x, velocity.y});
}

Vec2 Physics2D::getLinearVelocity(Entity entity) {
    RuntimeBodyData body;
    if (!findBody(entity, body)) { return Vec2(); }
    b2Vec2 result = b2Body_GetLinearVelocity(bodyIdOf(body));
    return Vec2(result.x, result.y);
}

float Physics2D::getMass(Entity entity) {
    RuntimeBodyData body;
    if (!findBody(entity, body)) { return 0; }
    return b2Body_GetMass(bodyIdOf(body));
}

float Physics2D::getFriction(Entity entity) {
    if (!isInitialized()) { return 0; }
    auto &bc2d = entity.getComponent<BoxCollider2DComponent>();
    b2ShapeId shapeId;
    memcpy(&shapeId, &bc2d.runtimeData, sizeof(b2ShapeId));
    return b2Shape_GetFriction(shapeId);
}

void Physics2D::setFriction(Entity entity, float friction) {
    if (!isInitialized()) { return; }
    auto &bc2d = entity.getComponent<BoxCollider2DComponent>();
    b2ShapeId shapeId;
    memcpy(&shapeId, &bc2d.runtimeData, sizeof(b2ShapeId));
    b2Shape_SetFriction(shapeId, friction);
}

void Physics2D::shutdown() {
    if (m_physicsWorldId) {
        b2WorldId id = IntToB2WorldId(m_physicsWorldId);
        b2DestroyWorld(id);
        m_physicsWorldId = 0;
    }
    m_bodyEntityMap.clear();
    m_bodies.clear();
    m_writePass = 0;
}

bool Physics2D::isRegistered(Entity entity) {
    return m_bodies.contains(entity.getId());
}

// A body is for an entity the world sees: a deleted one (the tombstone) has none.
bool Physics2D::canUse(Entity entity) {
    return entity.isValid() && entity.hasComponent<Rigidbody2DComponent>() &&
           entity.hasComponent<BoxCollider2DComponent>();
}

} // namespace Panda