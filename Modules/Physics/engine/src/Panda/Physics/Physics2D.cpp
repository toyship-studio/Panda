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

    template<typename TData>
    b2JointId jointIdOf(const TData &runtimeData) {
        b2JointId jointId;
        memcpy(&jointId, &runtimeData.data, sizeof(b2JointId));
        return jointId;
    }

    // The key of the live joint of a kind on an entity (Physics2D::m_joints).
    uint64_t jointKey(UUID entity, Joint2DKind kind) {
        return (static_cast<uint64_t>(static_cast<uint32_t>(entity)) << 8) | static_cast<uint64_t>(kind);
    }

    float angleOf(const TransformComponent &transform) {
        return glm::radians(transform.getRotationEuler().z);
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
    m_world = world;
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

// The joints of every entity, once the bodies exist.
void Physics2D::createJoints(World *world) {
    auto view = world->getAllEntitiesWith<IdComponent>();
    for (auto entityHandle : view) {
        if (!world->isValidEntt(entityHandle)) { continue; }
        createJointsOf(world->entity(entityHandle));
    }
}

void Physics2D::createJointsOf(Entity entity) {
    for (uint8_t kind = 0; kind < static_cast<uint8_t>(Joint2DKind::Count); kind++) {
        createJoint(entity, static_cast<Joint2DKind>(kind));
    }
}

// Authoring model: the anchors are in the local space of their bodies; the frame/reference angle
// fix the current pose.
bool Physics2D::createJoint(Entity entity, Joint2DKind kind) {
    if (!isInitialized() || !entity.isValid()) { return false; }
    b2WorldId worldId = IntToB2WorldId(m_physicsWorldId);
    // Both bodies must exist; an unconnected joint (connectedEntity=0) is silently skipped.
    auto bodies = [this, entity](UUID connectedId, b2BodyId &outBodyA, b2BodyId &outBodyB, Entity &outConnected) {
        if (!connectedId) { return false; }
        const RuntimeBodyData *body = m_bodies.find(entity.getId());
        if (body == nullptr) {
            LOG_ERROR_EDITOR("Joint on '%s': the entity has no body (a Rigidbody2D with a collider)", entity.getName().c_str());
            return false;
        }
        outConnected = m_world->getById(connectedId);
        const RuntimeBodyData *connectedBody = outConnected.isValid() ? m_bodies.find(connectedId) : nullptr;
        if (connectedBody == nullptr) {
            LOG_ERROR_EDITOR("Joint on '%s': connected entity has no Rigidbody2D", entity.getName().c_str());
            return false;
        }
        outBodyA = bodyIdOf(*body);
        outBodyB = bodyIdOf(*connectedBody);
        return true;
    };
    auto store = [this, entity, kind](b2JointId jointId) {
        RuntimeJointData runtimeData;
        memcpy(&runtimeData.data, &jointId, sizeof(b2JointId));
        m_joints[jointKey(entity.getId(), kind)] = runtimeData;
        return true;
    };
    b2BodyId bodyA, bodyB;
    Entity connected;
    switch (kind) {
        case Joint2DKind::Revolute: {
            if (!entity.hasComponent<RevoluteJoint2DComponent>()) { return false; }
            auto &joint = entity.getComponent<RevoluteJoint2DComponent>();
            if (!bodies(joint.connectedEntity, bodyA, bodyB, connected)) { return false; }
            auto ownerTransform = entity.calculateWorldSpaceTransform();
            auto connectedTransform = connected.calculateWorldSpaceTransform();
            const float ownerAngle = angleOf(ownerTransform);
            const float connectedAngle = angleOf(connectedTransform);
            const glm::vec2 anchorA = joint.anchor * glm::abs(glm::vec2(ownerTransform.getScale()));
            const glm::vec2 worldAnchor = glm::vec2(ownerTransform.getPosition()) + rotate2D(anchorA, ownerAngle);
            const glm::vec2 anchorB = rotate2D(worldAnchor - glm::vec2(connectedTransform.getPosition()), -connectedAngle);
            b2RevoluteJointDef def = b2DefaultRevoluteJointDef();
            def.bodyIdA = bodyA;
            def.bodyIdB = bodyB;
            def.collideConnected = joint.collideConnected;
            def.localAnchorA = {anchorA.x, anchorA.y};
            def.localAnchorB = {anchorB.x, anchorB.y};
            def.referenceAngle = connectedAngle - ownerAngle;
            def.enableLimit = joint.enableLimit;
            def.lowerAngle = glm::radians(joint.lowerAngle);
            def.upperAngle = glm::radians(joint.upperAngle);
            def.enableMotor = joint.enableMotor;
            def.motorSpeed = glm::radians(joint.motorSpeed);
            def.maxMotorTorque = joint.maxMotorTorque;
            return store(b2CreateRevoluteJoint(worldId, &def));
        }
        case Joint2DKind::Distance: {
            if (!entity.hasComponent<DistanceJoint2DComponent>()) { return false; }
            auto &joint = entity.getComponent<DistanceJoint2DComponent>();
            if (!bodies(joint.connectedEntity, bodyA, bodyB, connected)) { return false; }
            auto ownerTransform = entity.calculateWorldSpaceTransform();
            auto connectedTransform = connected.calculateWorldSpaceTransform();
            const float ownerAngle = angleOf(ownerTransform);
            const float connectedAngle = angleOf(connectedTransform);
            const glm::vec2 anchorA = joint.anchor * glm::abs(glm::vec2(ownerTransform.getScale()));
            const glm::vec2 anchorB = joint.connectedAnchor * glm::abs(glm::vec2(connectedTransform.getScale()));
            const glm::vec2 worldAnchorA = glm::vec2(ownerTransform.getPosition()) + rotate2D(anchorA, ownerAngle);
            const glm::vec2 worldAnchorB =
                glm::vec2(connectedTransform.getPosition()) + rotate2D(anchorB, connectedAngle);
            b2DistanceJointDef def = b2DefaultDistanceJointDef();
            def.bodyIdA = bodyA;
            def.bodyIdB = bodyB;
            def.collideConnected = joint.collideConnected;
            def.localAnchorA = {anchorA.x, anchorA.y};
            def.localAnchorB = {anchorB.x, anchorB.y};
            def.length = joint.length > 0.f ? joint.length : glm::distance(worldAnchorA, worldAnchorB);
            def.enableSpring = joint.enableSpring;
            def.hertz = joint.hertz;
            def.dampingRatio = joint.dampingRatio;
            return store(b2CreateDistanceJoint(worldId, &def));
        }
        case Joint2DKind::Count: break;
    }
    return false;
}

void Physics2D::destroyJoint(Entity entity, Joint2DKind kind) {
    const auto it = m_joints.find(jointKey(entity.getId(), kind));
    if (it == m_joints.end()) { return; }
    const b2JointId jointId = jointIdOf(it->second);
    if (b2Joint_IsValid(jointId)) { b2DestroyJoint(jointId); } // gone already when a body of it went
    m_joints.erase(it);
}

void Physics2D::destroyJointsOf(Entity entity) {
    for (uint8_t kind = 0; kind < static_cast<uint8_t>(Joint2DKind::Count); kind++) {
        destroyJoint(entity, static_cast<Joint2DKind>(kind));
    }
}

void Physics2D::recreateJointsTouching(Entity entity) {
    destroyJointsOf(entity);
    createJointsOf(entity);
    const UUID id = entity.getId();
    auto others = [this, id]<typename T>(Joint2DKind kind) {
        for (auto handle : m_world->getAllEntitiesWith<T>()) {
            if (!m_world->isValidEntt(handle)) { continue; }
            Entity other = m_world->entity(handle);
            if (other.getId() == id || other.getComponent<T>().connectedEntity != id) { continue; }
            destroyJoint(other, kind);
            createJoint(other, kind);
        }
    };
    others.template operator()<RevoluteJoint2DComponent>(Joint2DKind::Revolute);
    others.template operator()<DistanceJoint2DComponent>(Joint2DKind::Distance);
}

void Physics2D::jointComponentsUpdated(Entity entity) {
    if (!isInitialized()) { return; }
    destroyJointsOf(entity);
    createJointsOf(entity);
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
    const float rotationRad = angleOf(transform);
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
    // The entity's own collider, then those of the entities under it without a body of their own:
    // a compound body (Physics3D::registerEntity for the rules).
    addShapeOf(entity, entity, glm::vec2(0.f), 0.f, glm::abs(glm::vec2(transform.getScale())), runtimeData);
    std::vector<UUID> stack = entity.getChildEntities();
    while (!stack.empty()) {
        Entity part = m_world->getById(stack.back());
        stack.pop_back();
        if (!part.isValid() || part.hasComponent<Rigidbody2DComponent>() || m_leaving.contains(part.getId())) { continue; }
        for (UUID below : part.getChildEntities()) { stack.push_back(below); }
        if (!part.hasComponent<BoxCollider2DComponent>()) { continue; }
        CompoundPart record;
        record.entity = part;
        record.body = entity.getId();
        relativePose(entity, part, record.position, record.angle, record.scale);
        addShapeOf(entity, part, record.position, record.angle, record.scale, runtimeData);
        if (!m_compoundParts.contains(part.getId())) { m_world->trackEntity(part); }
        m_compoundParts[part.getId()] = record;
    }
    m_bodyEntityMap[bodyId.index1] = entity;
    m_bodies.add(entity, runtimeData);
}

void Physics2D::addShapeOf(
    Entity body, Entity owner, glm::vec2 relativePosition, float relativeAngle, glm::vec2 scale, RuntimeBodyData &runtimeData
) {
    if (!owner.hasComponent<BoxCollider2DComponent>()) { return; }
    auto &bc2d = owner.getComponent<BoxCollider2DComponent>();
    const glm::vec2 center = relativePosition + rotate2D(bc2d.offset, relativeAngle);
    b2Polygon boxShape = b2MakeOffsetBox(
        glm::abs(bc2d.size.x) * scale.x, glm::abs(bc2d.size.y) * scale.y, {center.x, center.y}, b2MakeRot(relativeAngle)
    );
    b2ShapeDef shapeDef = b2DefaultShapeDef();
    shapeDef.density = bc2d.density;
    shapeDef.friction = bc2d.friction;
    shapeDef.restitution = bc2d.restitution;
    shapeDef.isSensor = bc2d.isSensor;
    shapeDef.userData = reinterpret_cast<void *>(static_cast<uintptr_t>(static_cast<uint32_t>(owner.getId())));
    b2ShapeId shapeId = b2CreatePolygonShape(bodyIdOf(runtimeData), &shapeDef, &boxShape);
    memcpy(&bc2d.runtimeData, &shapeId, sizeof(b2ShapeId));
    (void)body;
}

bool Physics2D::hasShapesInSubtree(Entity entity) {
    if (entity.hasComponent<BoxCollider2DComponent>()) { return true; }
    std::vector<UUID> stack = entity.getChildEntities();
    while (!stack.empty()) {
        Entity part = m_world->getById(stack.back());
        stack.pop_back();
        if (!part.isValid() || part.hasComponent<Rigidbody2DComponent>() || m_leaving.contains(part.getId())) { continue; }
        if (part.hasComponent<BoxCollider2DComponent>()) { return true; }
        for (UUID below : part.getChildEntities()) { stack.push_back(below); }
    }
    return false;
}

Entity Physics2D::bodyEntityAbove(Entity entity) {
    if (entity.getWorld() == nullptr) { return {}; }
    UUID aboveId = entity.getComponent<RelationshipComponent>().getParent();
    while (aboveId != 0) {
        Entity above = m_world->getById(aboveId);
        if (above.getWorld() == nullptr) { break; }
        if (m_bodies.contains(aboveId)) { return above; }
        aboveId = above.getComponent<RelationshipComponent>().getParent();
    }
    return {};
}

void Physics2D::relativePose(Entity body, Entity entity, glm::vec2 &outPosition, float &outAngle, glm::vec2 &outScale) {
    const TransformComponent bodyWorld = body.calculateWorldSpaceTransform();
    const TransformComponent partWorld = entity.calculateWorldSpaceTransform();
    const float bodyAngle = angleOf(bodyWorld);
    outPosition = rotate2D(glm::vec2(partWorld.getPosition()) - glm::vec2(bodyWorld.getPosition()), -bodyAngle);
    outAngle = angleOf(partWorld) - bodyAngle;
    outScale = glm::abs(glm::vec2(partWorld.getScale()));
}

void Physics2D::rebuildBody(Entity entity, bool keepMotion) {
    const RuntimeBodyData *registered = m_bodies.find(entity.getId());
    if (registered == nullptr) {
        if (canUse(entity)) {
            registerEntity(entity);
            recreateJointsTouching(entity);
        }
        return;
    }
    const RuntimeBodyData old = *registered;
    const b2BodyId oldBodyId = bodyIdOf(old);
    const b2Vec2 position = b2Body_GetPosition(oldBodyId);
    const b2Rot rotation = b2Body_GetRotation(oldBodyId);
    const b2Vec2 linearVelocity = b2Body_GetLinearVelocity(oldBodyId);
    const float angularVelocity = b2Body_GetAngularVelocity(oldBodyId);
    removeEntity(entity);
    if (!canUse(entity)) { return; } // the joints went with the body
    registerEntity(entity);
    if (keepMotion) {
        RuntimeBodyData &body = *m_bodies.find(entity.getId());
        const b2BodyId bodyId = bodyIdOf(body);
        b2Body_SetTransform(bodyId, position, rotation);
        b2Body_SetLinearVelocity(bodyId, linearVelocity);
        b2Body_SetAngularVelocity(bodyId, angularVelocity);
        body.previousPosition = old.previousPosition;
        body.previousAngle = old.previousAngle;
    }
    recreateJointsTouching(entity);
}

void Physics2D::rebuildOwnersOf(Entity entity, bool keepMotion) {
    const auto part = m_compoundParts.find(entity.getId());
    const UUID current = part != m_compoundParts.end() ? part->second.body : UUID(0);
    Entity above = bodyEntityAbove(entity);
    if (current != 0) {
        Entity owner = m_world->getById(current);
        if (owner.getWorld() != nullptr && isRegistered(owner)) { rebuildBody(owner, keepMotion); }
    }
    if (above.getWorld() != nullptr && above.getId() != current) { rebuildBody(above, keepMotion); }
}

void Physics2D::componentsUpdated(Entity entity) {
    if (!isInitialized()) { return; }
    rebuildBody(entity, true);
    rebuildOwnersOf(entity, true);
}

void Physics2D::entityRemoved(Entity entity) {
    if (!isInitialized()) { return; }
    if (isRegistered(entity)) { removeEntity(entity); }
    const auto part = m_compoundParts.find(entity.getId());
    if (part != m_compoundParts.end()) {
        m_world->untrackEntity(part->second.entity);
        m_compoundParts.erase(part);
    }
    m_leaving.insert(entity.getId());
    Entity above = bodyEntityAbove(entity);
    if (above.getWorld() != nullptr) { rebuildBody(above, true); }
    m_leaving.erase(entity.getId());
}

void Physics2D::entityRestored(Entity entity) {
    if (!isInitialized()) { return; }
    componentsUpdated(entity);
}

void Physics2D::syncTransform(Panda::Entity entity) {
    // Body teleport (position/rotation changed from outside: gizmo/script/inspector). Without
    // rebuilding the shapes — cheap for a per-frame drag. The velocities are zeroed ("put it down —
    // it stays", like propertiesUpdated), wake — a teleported body must recompute its contacts.
    if (!isInitialized()) { return; }
    RuntimeBodyData *registered = m_bodies.find(entity.getId());
    if (registered == nullptr) {
        // A part of a compound body: the body moved as a whole (nothing to do) or the part moved
        // within it (the body is rebuilt around it).
        const auto part = m_compoundParts.find(entity.getId());
        if (part == m_compoundParts.end()) { return; }
        Entity owner = m_world->getById(part->second.body);
        if (owner.getWorld() == nullptr || !isRegistered(owner)) { return; }
        glm::vec2 position;
        float angle = 0.f;
        glm::vec2 scale;
        relativePose(owner, entity, position, angle, scale);
        const CompoundPart &record = part->second;
        const bool samePlace = glm::all(glm::epsilonEqual(position, record.position, 1e-5f)) &&
                               glm::abs(angle - record.angle) < 1e-5f &&
                               glm::all(glm::epsilonEqual(scale, record.scale, 1e-5f));
        Entity above = bodyEntityAbove(entity);
        if (samePlace && above.getWorld() != nullptr && above.getId() == record.body) { return; }
        rebuildOwnersOf(entity, true);
        return;
    }
    RuntimeBodyData &runtimeData = *registered;
    const b2BodyId bodyId = bodyIdOf(runtimeData);
    auto transform = entity.calculateWorldSpaceTransform();
    const float rotationRad = angleOf(transform);
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
    // The body is made again at the entity's transform, at rest; a part of a compound body
    // rebuilds its body.
    if (isRegistered(entity)) {
        rebuildBody(entity, false);
    } else {
        rebuildOwnersOf(entity, false);
    }
}

void Physics2D::removeEntity(Entity entity) {
    if (!isInitialized()) { return; }
    const RuntimeBodyData *registered = m_bodies.find(entity.getId());
    if (registered == nullptr) { return; }
    const b2BodyId bodyId = bodyIdOf(*registered);
    b2DestroyBody(bodyId); // its shapes and joints go with it
    m_bodyEntityMap.erase(bodyId.index1);
    for (uint8_t kind = 0; kind < static_cast<uint8_t>(Joint2DKind::Count); kind++) {
        m_joints.erase(jointKey(entity.getId(), static_cast<Joint2DKind>(kind)));
    }
    for (auto it = m_compoundParts.begin(); it != m_compoundParts.end();) {
        if (it->second.body == entity.getId()) {
            m_world->untrackEntity(it->second.entity);
            it = m_compoundParts.erase(it);
        } else {
            ++it;
        }
    }
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
    for (auto &[partId, part] : m_compoundParts) {
        m_world->untrackEntity(part.entity);
    }
    m_compoundParts.clear();
    m_leaving.clear();
    m_joints.clear();
    m_writePass = 0;
    m_world = nullptr;
}

bool Physics2D::isRegistered(Entity entity) {
    return m_bodies.contains(entity.getId());
}

// A body is for an entity the world sees: a deleted one (the tombstone) has none. It needs a
// shape: a collider of its own, or one on an entity under it without a body of its own.
bool Physics2D::canUse(Entity entity) {
    return entity.isValid() && entity.hasComponent<Rigidbody2DComponent>() && hasShapesInSubtree(entity);
}

} // namespace Panda