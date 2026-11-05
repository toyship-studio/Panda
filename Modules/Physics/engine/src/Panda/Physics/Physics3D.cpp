#include "Panda/Physics/Physics3D.hpp"
#include "Panda/GameLogic/World.hpp"
#include "Panda/Physics/Box3DHandles.hpp"
#include "Panda/Physics/MeshColliderGeometry.hpp"

#include <cfloat>

namespace Panda {

// The geometry of a mesh collider as box3d takes it, built from the triangles of the source on
// demand: the triangle mesh with its BVH for the mesh shapes of static bodies (a shape references
// the data, so it lives as long as the last body built from it), the convex hull of the points for
// the hull shapes of moving bodies (box3d clones a hull into its database, the base one is reused).
struct MeshColliderCook {
    MeshGeometry geometry;
    b3MeshData *mesh = nullptr;
    b3HullData *hull = nullptr;
    bool meshFailed = false;
    bool hullFailed = false;

    MeshColliderCook() = default;
    MeshColliderCook(const MeshColliderCook &) = delete;
    MeshColliderCook &operator=(const MeshColliderCook &) = delete;
    ~MeshColliderCook() {
        if (mesh != nullptr) { b3DestroyMesh(mesh); }
        if (hull != nullptr) { b3DestroyHull(hull); }
    }

    b3MeshData *triangleMesh() {
        if (mesh != nullptr || meshFailed) { return mesh; }
        std::vector<b3Vec3> vertices(geometry.positions.size());
        for (size_t i = 0; i < vertices.size(); i++) {
            vertices[i] = {geometry.positions[i].x, geometry.positions[i].y, geometry.positions[i].z};
        }
        std::vector<int32_t> indices(geometry.indices.begin(), geometry.indices.end());
        b3MeshDef def = {};
        def.vertices = vertices.data();
        def.indices = indices.data();
        def.materialIndices = nullptr;
        def.vertexCount = static_cast<int>(vertices.size());
        def.triangleCount = static_cast<int>(indices.size() / 3);
        // A rendering mesh splits its vertices along UV seams and hard edges: welded, the
        // triangles share their edges and the contacts do not snag on internal ones.
        def.weldVertices = true;
        def.weldTolerance = 1e-4f;
        def.useMedianSplit = false; // SAH: the triangles of a model are not a grid
        def.identifyEdges = true;
        mesh = b3CreateMesh(&def, nullptr, 0); // box3d copies the arrays
        meshFailed = mesh == nullptr;
        return mesh;
    }

    b3HullData *convexHull() {
        if (hull != nullptr || hullFailed) { return hull; }
        std::vector<b3Vec3> points(geometry.positions.size());
        for (size_t i = 0; i < points.size(); i++) {
            points[i] = {geometry.positions[i].x, geometry.positions[i].y, geometry.positions[i].z};
        }
        // box3d keeps at most 255 hull vertices and simplifies the hull to the budget itself.
        hull = points.size() >= 4 ? b3CreateHull(points.data(), static_cast<int>(points.size()), UINT8_MAX) : nullptr;
        hullFailed = hull == nullptr;
        return hull;
    }
};

namespace {
    // Allowed penetration of the mover into a dynamic body (soft plane), m. The penetration depth
    // filled in by the solver in plane.push scales the push force: a touch without pressure — no
    // force.
    constexpr float MOVER_DYNAMIC_PUSH_LIMIT = 0.025f;

    // Callbacks of the box3d character mover: the own shape and sensors are skipped, the contact
    // planes are accumulated. Static/kinematic bodies are rigid walls; dynamic bodies are soft +
    // remembered for pushing.
    struct MoverDynamicContact {
        b3BodyId body;
        b3Vec3 normal; // outward from the shape (towards the mover)
        b3Vec3 point;  // contact point relative to the collide origin
        int planeIndex;
    };
    struct MoverContext {
        b3BodyId selfBody;
        b3CollisionPlane planes[16];
        int planeCount = 0;
        MoverDynamicContact dynamicContacts[16];
        int dynamicContactCount = 0;
        bool grounded = false;
    };

    bool moverShapeFilter(b3ShapeId shapeId, void *context) {
        MoverContext *ctx = static_cast<MoverContext *>(context);
        if (b3Shape_IsSensor(shapeId)) { return false; }
        b3BodyId body = b3Shape_GetBody(shapeId);
        return body.index1 != ctx->selfBody.index1;
    }

    bool moverPlaneResult(b3ShapeId shapeId, const b3PlaneResult *planes, int planeCount, void *context) {
        MoverContext *ctx = static_cast<MoverContext *>(context);
        if (!moverShapeFilter(shapeId, context)) { return true; }
        b3BodyId body = b3Shape_GetBody(shapeId);
        const bool dynamic = b3Body_GetType(body) == b3_dynamicBody;
        for (int i = 0; i < planeCount && ctx->planeCount < 16; ++i) {
            if (dynamic) {
                if (ctx->dynamicContactCount < 16) {
                    ctx->dynamicContacts[ctx->dynamicContactCount++] = {
                        body, planes[i].plane.normal, planes[i].point, ctx->planeCount};
                }
                ctx->planes[ctx->planeCount++] = {planes[i].plane, MOVER_DYNAMIC_PUSH_LIMIT, 0.f, false};
            } else {
                ctx->planes[ctx->planeCount++] = {planes[i].plane, FLT_MAX, 0.f, true};
            }
            if (planes[i].plane.normal.y > 0.7f) { ctx->grounded = true; } // support: slope up to ~45°
        }
        return ctx->planeCount < 16;
    }

    // box3d identifiers from their opaque storage in the Physics3D maps (the engine headers do
    // not pull in box3d).
    template<typename TData>
    b3BodyId bodyIdOf(const TData &runtimeData) {
        b3BodyId bodyId;
        memcpy(&bodyId, &runtimeData.data, sizeof(b3BodyId));
        return bodyId;
    }

    template<typename TData>
    b3JointId jointIdOf(const TData &runtimeData) {
        b3JointId jointId;
        memcpy(&jointId, &runtimeData.data, sizeof(b3JointId));
        return jointId;
    }

    // Why a mesh collider has no shape: the game log and the editor console.
    void warnMeshCollider(Entity entity, const char *reason) {
        LOG_WARN("Mesh Collider 3D on '%s': %s", entity.getName().c_str(), reason);
        LOG_WARN_EDITOR("Mesh Collider 3D on '%s': %s", entity.getName().c_str(), reason);
    }

    // The shape of a mesh collider on the body: the convex hull scaled by the entity, or the
    // triangles themselves on a static body. Null — no shape, the reason is in the log.
    b3ShapeId createMeshColliderShape(
        b3BodyId bodyId, const b3ShapeDef &shapeDef, MeshColliderCook &cook, bool convex, bool staticBody,
        glm::vec3 scale, Entity entity
    ) {
        const b3Vec3 b3Scale = {scale.x, scale.y, scale.z};
        if (convex) {
            b3HullData *hull = cook.convexHull();
            if (hull == nullptr) {
                warnMeshCollider(entity, "the mesh gives no convex hull (fewer than four points off a plane)");
                return b3_nullShapeId;
            }
            return b3CreateTransformedHullShape(bodyId, &shapeDef, hull, b3Transform_identity, b3Scale);
        }
        if (!staticBody) {
            warnMeshCollider(
                entity, "a non-convex mesh collides on a static body only; make the collider convex or the body static"
            );
            return b3_nullShapeId;
        }
        b3MeshData *mesh = cook.triangleMesh();
        if (mesh == nullptr) {
            warnMeshCollider(entity, "the triangles give no collision mesh");
            return b3_nullShapeId;
        }
        return b3CreateMeshShape(bodyId, &shapeDef, mesh, b3Scale);
    }

    // Closest hit of a ray: every accepted shape clips the ray, so the last one written is the
    // closest. Sensors and the ignored body are passed through.
    struct RaycastContext {
        b3BodyId ignoredBody = b3_nullBodyId;
        bool hit = false;
        b3ShapeId shape;
        b3Pos point;
        b3Vec3 normal;
        float fraction = 1.f;
    };

    float raycastClosestResult(
        b3ShapeId shapeId, b3Pos point, b3Vec3 normal, float fraction, uint64_t, int, int, void *context
    ) {
        RaycastContext *ctx = static_cast<RaycastContext *>(context);
        if (b3Shape_IsSensor(shapeId)) { return -1.f; }
        if (!B3_IS_NULL(ctx->ignoredBody) && b3Shape_GetBody(shapeId).index1 == ctx->ignoredBody.index1) {
            return -1.f;
        }
        ctx->hit = true;
        ctx->shape = shapeId;
        ctx->point = point;
        ctx->normal = normal;
        ctx->fraction = fraction;
        return fraction;
    }
} // namespace

inline b3BodyType rigidbody3DTypeToBox3DBody(Rigidbody3DComponent::BodyType bodyType) {
    switch (bodyType) {
        case Rigidbody3DComponent::BodyType::STATIC:
            return b3_staticBody;
        case Rigidbody3DComponent::BodyType::DYNAMIC:
            return b3_dynamicBody;
        case Rigidbody3DComponent::BodyType::KINEMATIC:
            return b3_kinematicBody;
    }
    PND_ASSERT(false, "Unknown body type");
    return b3_staticBody;
}


inline b3Quat toB3Quat(const glm::quat &q) {
    b3Quat result;
    result.v = {q.x, q.y, q.z};
    result.s = q.w;
    return result;
}

void Physics3D::init(World *world) {
    PND_STATIC_ASSERT(sizeof(b3BodyId) <= sizeof(RuntimeBodyData::data), "Rigidbody3D runtime body wrong data size");
    PND_STATIC_ASSERT(
        sizeof(b3ShapeId) <= sizeof(BoxCollider3DComponent::runtimeData), "Rigidbody3D runtime shape wrong data size"
    );
    PND_STATIC_ASSERT(
        sizeof(b3ShapeId) <= sizeof(SphereCollider3DComponent::runtimeData), "Rigidbody3D runtime shape wrong data size"
    );
    PND_STATIC_ASSERT(
        sizeof(b3ShapeId) <= sizeof(CapsuleCollider3DComponent::runtimeData), "Rigidbody3D runtime shape wrong data size"
    );
    PND_STATIC_ASSERT(
        sizeof(b3ShapeId) <= sizeof(MeshCollider3DComponent::runtimeData), "Rigidbody3D runtime shape wrong data size"
    );
    PND_STATIC_ASSERT(sizeof(b3JointId) <= sizeof(RuntimeJointData::data), "Joint3D runtime joint wrong data size");
    m_world = world;
    b3WorldDef worldDef = b3DefaultWorldDef();
    b3WorldId worldId = b3CreateWorld(&worldDef);
    m_physicsWorldId = b3WorldIdToInt(worldId);
    // Register all entities
    auto view = world->getAllEntitiesWith<Rigidbody3DComponent>();
    for (auto entityHandle : view) {
        if (!world->isValidEntt(entityHandle)) { continue; }
        Entity entity = world->entity(entityHandle);
        registerEntity(entity);
    }
    // Joints — in a second pass: both bodies must exist by now (the bodies of the world systems,
    // terrain among them, come with their own start hooks after the physics).
    createJoints(world);
}

// Quaternion of the shortest rotation of the joint frame's Z axis onto the authored axis.
static glm::quat axisFrameRotation(glm::vec3 axis) {
    const glm::vec3 z(0.f, 0.f, 1.f);
    const float lengthSq = glm::dot(axis, axis);
    const glm::vec3 a = lengthSq > 1e-8f ? axis / glm::sqrt(lengthSq) : z;
    const float d = glm::dot(z, a);
    if (d > 0.9999f) { return glm::quat(1.f, 0.f, 0.f, 0.f); }
    if (d < -0.9999f) { return glm::quat(0.f, 1.f, 0.f, 0.f); } // 180° around X
    const glm::vec3 c = glm::cross(z, a);
    return glm::normalize(glm::quat(1.f + d, c.x, c.y, c.z));
}

// Joint frames from the authoring model "anchor+axis in the owner's local space": frame B is
// computed so as to fix the CURRENT relative pose of the bodies (the limits are measured from it).
static void makeJointFrames(
    Entity entity, Entity connected, glm::vec3 anchor, glm::vec3 axis, b3Transform &outFrameA,
    b3Transform &outFrameB
) {
    TransformComponent ownerTransform = entity.calculateWorldSpaceTransform();
    TransformComponent connectedTransform = connected.calculateWorldSpaceTransform();
    const glm::quat ownerRotation = ownerTransform.getRotation();
    const glm::quat connectedRotation = connectedTransform.getRotation();
    const glm::vec3 scaledAnchor = anchor * glm::abs(ownerTransform.getScale());
    const glm::quat frameRotation = axisFrameRotation(axis);
    const glm::vec3 worldAnchor = ownerTransform.getPosition() + ownerRotation * scaledAnchor;
    const glm::quat worldFrameRotation = ownerRotation * frameRotation;
    const glm::quat inverseConnected = glm::inverse(connectedRotation);
    const glm::vec3 localAnchorB = inverseConnected * (worldAnchor - connectedTransform.getPosition());
    const glm::quat localFrameRotationB = inverseConnected * worldFrameRotation;
    outFrameA.p = {scaledAnchor.x, scaledAnchor.y, scaledAnchor.z};
    outFrameA.q = toB3Quat(frameRotation);
    outFrameB.p = {localAnchorB.x, localAnchorB.y, localAnchorB.z};
    outFrameB.q = toB3Quat(localFrameRotationB);
}

// Wheel joint frames from the authoring model "anchor + spin axis + suspension axis in the wheel's
// local space". The box3d convention: X of the joint frame is the suspension (and steering) axis,
// Z — the spin axis; the suspension axis is made orthogonal to the spin axis. The chassis frame is
// the same world frame in the chassis space, so the pose at start is the rest position of the
// spring and the zero steering angle.
static void makeWheelJointFrames(
    Entity wheel, Entity chassis, glm::vec3 anchor, glm::vec3 axis, glm::vec3 suspensionAxis,
    b3Transform &outChassisFrame, b3Transform &outWheelFrame
) {
    TransformComponent wheelTransform = wheel.calculateWorldSpaceTransform();
    TransformComponent chassisTransform = chassis.calculateWorldSpaceTransform();
    const glm::quat wheelRotation = wheelTransform.getRotation();
    const glm::quat inverseChassis = glm::inverse(chassisTransform.getRotation());
    const glm::vec3 scaledAnchor = anchor * glm::abs(wheelTransform.getScale());
    const glm::vec3 z = glm::dot(axis, axis) > 1e-8f ? glm::normalize(axis) : glm::vec3(1.f, 0.f, 0.f);
    glm::vec3 x = suspensionAxis - z * glm::dot(suspensionAxis, z);
    if (glm::dot(x, x) < 1e-8f) {
        // The suspension axis is missing or parallel to the spin axis: any perpendicular will do.
        const glm::vec3 other = glm::abs(z.y) < 0.9f ? glm::vec3(0.f, 1.f, 0.f) : glm::vec3(1.f, 0.f, 0.f);
        x = other - z * glm::dot(other, z);
    }
    x = glm::normalize(x);
    const glm::quat frameRotation = glm::normalize(glm::quat_cast(glm::mat3(x, glm::cross(z, x), z)));
    const glm::vec3 worldAnchor = wheelTransform.getPosition() + wheelRotation * scaledAnchor;
    const glm::vec3 chassisAnchor = inverseChassis * (worldAnchor - chassisTransform.getPosition());
    outWheelFrame.p = {scaledAnchor.x, scaledAnchor.y, scaledAnchor.z};
    outWheelFrame.q = toB3Quat(frameRotation);
    outChassisFrame.p = {chassisAnchor.x, chassisAnchor.y, chassisAnchor.z};
    outChassisFrame.q = toB3Quat(glm::normalize(inverseChassis * wheelRotation * frameRotation));
}

void Physics3D::createJoints(World *world) {
    b3WorldId worldId = intToB3WorldId(m_physicsWorldId);
    // Both bodies must exist; an unconnected joint (connectedEntity=0) is silently skipped.
    auto findJointBodies = [this, world](
                               Entity entity, UUID connectedId, b3BodyId &outBodyA, b3BodyId &outBodyB,
                               Entity &outConnected
                           ) {
        if (!connectedId) { return false; }
        const RuntimeBodyData *body = m_bodies.find(entity.getId());
        if (body == nullptr) { return false; }
        outConnected = world->getById(connectedId);
        const RuntimeBodyData *connectedBody = outConnected.isValid() ? m_bodies.find(connectedId) : nullptr;
        if (connectedBody == nullptr) {
            LOG_ERROR_EDITOR("Joint on '%s': connected entity has no Rigidbody3D", entity.getName().c_str());
            return false;
        }
        outBodyA = bodyIdOf(*body);
        outBodyB = bodyIdOf(*connectedBody);
        return true;
    };
    auto revoluteView = world->getAllEntitiesWith<RevoluteJoint3DComponent>();
    for (auto entityHandle : revoluteView) {
        if (!world->isValidEntt(entityHandle)) { continue; }
        Entity entity = world->entity(entityHandle);
        auto &joint = entity.getComponent<RevoluteJoint3DComponent>();
        b3BodyId bodyA, bodyB;
        Entity connected;
        if (!findJointBodies(entity, joint.connectedEntity, bodyA, bodyB, connected)) { continue; }
        b3RevoluteJointDef def = b3DefaultRevoluteJointDef();
        def.base.bodyIdA = bodyA;
        def.base.bodyIdB = bodyB;
        makeJointFrames(entity, connected, joint.anchor, joint.axis, def.base.localFrameA, def.base.localFrameB);
        def.enableLimit = joint.enableLimit;
        def.lowerAngle = glm::radians(joint.lowerAngle);
        def.upperAngle = glm::radians(joint.upperAngle);
        def.enableMotor = joint.enableMotor;
        def.motorSpeed = glm::radians(joint.motorSpeed);
        def.maxMotorTorque = joint.maxMotorTorque;
        b3CreateRevoluteJoint(worldId, &def);
    }
    auto sphericalView = world->getAllEntitiesWith<SphericalJoint3DComponent>();
    for (auto entityHandle : sphericalView) {
        if (!world->isValidEntt(entityHandle)) { continue; }
        Entity entity = world->entity(entityHandle);
        auto &joint = entity.getComponent<SphericalJoint3DComponent>();
        b3BodyId bodyA, bodyB;
        Entity connected;
        if (!findJointBodies(entity, joint.connectedEntity, bodyA, bodyB, connected)) { continue; }
        b3SphericalJointDef def = b3DefaultSphericalJointDef();
        def.base.bodyIdA = bodyA;
        def.base.bodyIdB = bodyB;
        makeJointFrames(entity, connected, joint.anchor, joint.axis, def.base.localFrameA, def.base.localFrameB);
        def.enableConeLimit = joint.enableConeLimit;
        def.coneAngle = glm::radians(joint.coneAngle);
        def.enableTwistLimit = joint.enableTwistLimit;
        def.lowerTwistAngle = glm::radians(joint.lowerTwistAngle);
        def.upperTwistAngle = glm::radians(joint.upperTwistAngle);
        b3CreateSphericalJoint(worldId, &def);
    }
    auto wheelView = world->getAllEntitiesWith<WheelJoint3DComponent>();
    for (auto entityHandle : wheelView) {
        if (!world->isValidEntt(entityHandle)) { continue; }
        Entity entity = world->entity(entityHandle);
        auto &joint = entity.getComponent<WheelJoint3DComponent>();
        b3BodyId wheelBody, chassisBody;
        Entity chassis;
        if (!findJointBodies(entity, joint.connectedEntity, wheelBody, chassisBody, chassis)) { continue; }
        b3WheelJointDef def = b3DefaultWheelJointDef();
        // In box3d body A is the chassis and body B is the wheel, while the component sits on the wheel.
        def.base.bodyIdA = chassisBody;
        def.base.bodyIdB = wheelBody;
        makeWheelJointFrames(
            entity, chassis, joint.anchor, joint.axis, joint.suspensionAxis, def.base.localFrameA, def.base.localFrameB
        );
        def.enableSuspensionSpring = joint.enableSuspensionSpring;
        def.suspensionHertz = glm::max(joint.suspensionHertz, 0.f);
        def.suspensionDampingRatio = glm::max(joint.suspensionDampingRatio, 0.f);
        def.enableSuspensionLimit = joint.enableSuspensionLimit;
        def.lowerSuspensionLimit = glm::min(joint.lowerSuspensionLimit, joint.upperSuspensionLimit);
        def.upperSuspensionLimit = glm::max(joint.lowerSuspensionLimit, joint.upperSuspensionLimit);
        def.enableSteering = joint.enableSteering;
        def.steeringHertz = glm::max(joint.steeringHertz, 0.f);
        def.steeringDampingRatio = glm::max(joint.steeringDampingRatio, 0.f);
        def.maxSteeringTorque = glm::max(joint.maxSteeringTorque, 0.f);
        def.enableSteeringLimit = joint.enableSteeringLimit;
        def.lowerSteeringLimit = glm::radians(glm::min(joint.lowerSteeringAngle, joint.upperSteeringAngle));
        def.upperSteeringLimit = glm::radians(glm::max(joint.lowerSteeringAngle, joint.upperSteeringAngle));
        def.enableSpinMotor = joint.enableMotor;
        def.spinSpeed = glm::radians(joint.motorSpeed);
        def.maxSpinTorque = glm::max(joint.maxMotorTorque, 0.f);
        const b3JointId jointId = b3CreateWheelJoint(worldId, &def);
        RuntimeJointData runtimeData;
        memcpy(&runtimeData.data, &jointId, sizeof(b3JointId));
        m_wheelJointMap[entity.getId()] = runtimeData;
    }
}

bool Physics3D::isInitialized() {
    return m_physicsWorldId;
}

void Physics3D::beginStep() {
    m_bodies.forEach([](RuntimeBodyData &body) {
        const b3BodyId bodyId = bodyIdOf(body);
        const b3Pos position = b3Body_GetPosition(bodyId);
        const b3Quat rotation = b3Body_GetRotation(bodyId);
        body.previousPosition = {(float)position.x, (float)position.y, (float)position.z};
        body.previousRotation = {rotation.s, rotation.v.x, rotation.v.y, rotation.v.z};
    });
    m_stepping = true;
}

void Physics3D::step(World *world, double stepTime) {
    const int32_t subStepCount = 4;
    b3WorldId worldId = intToB3WorldId(m_physicsWorldId);
    b3World_Step(worldId, stepTime, subStepCount);
    writeTransforms(world, 1.f);
    //----------------------------------//
    //          CONTACT EVENTS          //
    //----------------------------------//
    b3ContactEvents contactEvents = b3World_GetContactEvents(worldId);
    for (int i = 0; i < contactEvents.endCount; ++i) {
        b3ContactEndTouchEvent event = contactEvents.endEvents[i];
        if (!b3Shape_IsValid(event.shapeIdA) || !b3Shape_IsValid(event.shapeIdB)) { continue; }
        b3BodyId bodyIdA = b3Shape_GetBody(event.shapeIdA);
        b3BodyId bodyIdB = b3Shape_GetBody(event.shapeIdB);
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
        b3ContactBeginTouchEvent event = contactEvents.beginEvents[i];
        if (!b3Shape_IsValid(event.shapeIdA) || !b3Shape_IsValid(event.shapeIdB)) { continue; }
        b3BodyId bodyIdA = b3Shape_GetBody(event.shapeIdA);
        b3BodyId bodyIdB = b3Shape_GetBody(event.shapeIdB);
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
    b3SensorEvents sensorEvents = b3World_GetSensorEvents(worldId);
    for (int i = 0; i < sensorEvents.beginCount; ++i) {
        b3SensorBeginTouchEvent event = sensorEvents.beginEvents[i];
        if (!b3Shape_IsValid(event.sensorShapeId) || !b3Shape_IsValid(event.visitorShapeId)) { continue; }
        b3BodyId bodyIdSensor = b3Shape_GetBody(event.sensorShapeId);
        b3BodyId bodyIdVisitor = b3Shape_GetBody(event.visitorShapeId);
        Entity entitySensor = m_bodyEntityMap.at(bodyIdSensor.index1);
        Entity entityVisitor = m_bodyEntityMap.at(bodyIdVisitor.index1);
        if (!entitySensor.isValid() || !entityVisitor.isValid()) { continue; }
        ScriptListComponent &scriptListVisitor = entityVisitor.getComponent<ScriptListComponent>();
        for (ExternalScript &script : scriptListVisitor.scripts) {
            script.invokeBeginSensorOverlap(entitySensor.getId());
        }
    }
    for (int i = 0; i < sensorEvents.endCount; ++i) {
        b3SensorEndTouchEvent event = sensorEvents.endEvents[i];
        if (!b3Shape_IsValid(event.sensorShapeId) || !b3Shape_IsValid(event.visitorShapeId)) { continue; }
        b3BodyId bodyIdSensor = b3Shape_GetBody(event.sensorShapeId);
        b3BodyId bodyIdVisitor = b3Shape_GetBody(event.visitorShapeId);
        Entity entitySensor = m_bodyEntityMap.at(bodyIdSensor.index1);
        Entity entityVisitor = m_bodyEntityMap.at(bodyIdVisitor.index1);
        if (!entitySensor.isValid() || !entityVisitor.isValid()) { continue; }
        ScriptListComponent &scriptListVisitor = entityVisitor.getComponent<ScriptListComponent>();
        for (ExternalScript &script : scriptListVisitor.scripts) {
            script.invokeEndSensorOverlap(entitySensor.getId());
        }
    }
    m_stepping = false;
}

void Physics3D::writeTransforms(World *world, float alpha) {
    m_writePass++;
    auto view = world->getAllEntitiesWith<Rigidbody3DComponent>();
    for (auto entityHandle : view) {
        if (!world->isValidEntt(entityHandle)) { continue; }
        writeTransform(world->entity(entityHandle), alpha);
    }
}

void Physics3D::writeTransform(Entity entity, float alpha) {
    RuntimeBodyData *body = m_bodies.find(entity.getId());
    if (body != nullptr && body->writePass == m_writePass) { return; }
    // Everything above the entity first; an entity without a body only passes the call up.
    Entity parent = entity.getParent();
    if (parent.isValid()) { writeTransform(parent, alpha); }
    if (body == nullptr) { return; }
    body->writePass = m_writePass;
    const b3BodyId bodyId = bodyIdOf(*body);
    const b3Pos stepped = b3Body_GetPosition(bodyId);
    const b3Quat turned = b3Body_GetRotation(bodyId);
    glm::vec3 position((float)stepped.x, (float)stepped.y, (float)stepped.z);
    glm::quat rotation(turned.s, turned.v.x, turned.v.y, turned.v.z);
    if (alpha < 1.f) {
        position = glm::mix(body->previousPosition, position, alpha);
        rotation = glm::slerp(body->previousRotation, rotation, alpha);
    }
    entity.getWorld()->setWorldPose(entity, position, rotation);
}

void Physics3D::registerEntity(Entity entity) {
    if (!isInitialized()) { return; }
    b3WorldId worldId = intToB3WorldId(m_physicsWorldId);
    auto transform = entity.calculateWorldSpaceTransform();
    auto &rb3d = entity.getComponent<Rigidbody3DComponent>();
    b3BodyDef bodyDef = b3DefaultBodyDef();
    bodyDef.type = rigidbody3DTypeToBox3DBody(rb3d.type);
    bodyDef.position = {transform.getPosition().x, transform.getPosition().y, transform.getPosition().z};
    bodyDef.rotation = toB3Quat(transform.getRotation());
    bodyDef.motionLocks.angularX = rb3d.fixedRotation;
    bodyDef.motionLocks.angularY = rb3d.fixedRotation;
    bodyDef.motionLocks.angularZ = rb3d.fixedRotation;
    // A wheel outruns the angular speed cap of box3d (a quarter of a turn per step: about 60 km/h
    // for a car wheel at 60 fps); the flag is accepted only at body creation.
    bodyDef.allowFastRotation = entity.hasComponent<WheelJoint3DComponent>();
    b3BodyId bodyId = b3CreateBody(worldId, &bodyDef);
    RuntimeBodyData runtimeData;
    memcpy(&runtimeData.data, &bodyId, sizeof(b3BodyId));
    runtimeData.previousPosition = transform.getPosition();
    runtimeData.previousRotation = transform.getRotation();
    const glm::vec3 scale = glm::abs(transform.getScale());
    // Common properties of a collider shape (density/friction/restitution/sensor + events for
    // scripts).
    auto makeShapeDef = [](bool isSensor, float density, float friction, float restitution) {
        b3ShapeDef shapeDef = b3DefaultShapeDef();
        shapeDef.density = density;
        shapeDef.baseMaterial.friction = friction;
        shapeDef.baseMaterial.restitution = restitution;
        shapeDef.isSensor = isSensor;
        // In box3d the events are off by default — they are enabled so that scripts receive
        // begin/end touch.
        shapeDef.enableSensorEvents = true;
        shapeDef.enableContactEvents = true;
        return shapeDef;
    };
    if (entity.hasComponent<BoxCollider3DComponent>()) {
        auto &bc3d = entity.getComponent<BoxCollider3DComponent>();
        const glm::vec3 half = glm::abs(bc3d.size) * scale;
        b3BoxHull boxHull =
            b3MakeOffsetBoxHull(half.x, half.y, half.z, {bc3d.offset.x, bc3d.offset.y, bc3d.offset.z});
        b3ShapeDef shapeDef = makeShapeDef(bc3d.isSensor, bc3d.density, bc3d.friction, bc3d.restitution);
        b3ShapeId shapeId = b3CreateHullShape(bodyId, &shapeDef, &boxHull.base);
        memcpy(&bc3d.runtimeData, &shapeId, sizeof(b3ShapeId));
    }
    if (entity.hasComponent<SphereCollider3DComponent>()) {
        auto &sc3d = entity.getComponent<SphereCollider3DComponent>();
        b3Sphere sphere;
        sphere.center = {sc3d.offset.x, sc3d.offset.y, sc3d.offset.z};
        sphere.radius = glm::abs(sc3d.radius) * glm::max(scale.x, glm::max(scale.y, scale.z));
        b3ShapeDef shapeDef = makeShapeDef(sc3d.isSensor, sc3d.density, sc3d.friction, sc3d.restitution);
        b3ShapeId shapeId = b3CreateSphereShape(bodyId, &shapeDef, &sphere);
        memcpy(&sc3d.runtimeData, &shapeId, sizeof(b3ShapeId));
    }
    if (entity.hasComponent<CapsuleCollider3DComponent>()) {
        auto &cc3d = entity.getComponent<CapsuleCollider3DComponent>();
        // The capsule axis is the local Y: the radius is scaled by XZ, the half-height — by Y (as
        // in Unity).
        const float halfHeight = glm::abs(cc3d.halfHeight) * scale.y;
        b3Capsule capsule;
        capsule.center1 = {cc3d.offset.x, cc3d.offset.y - halfHeight, cc3d.offset.z};
        capsule.center2 = {cc3d.offset.x, cc3d.offset.y + halfHeight, cc3d.offset.z};
        capsule.radius = glm::abs(cc3d.radius) * glm::max(scale.x, scale.z);
        b3ShapeDef shapeDef = makeShapeDef(cc3d.isSensor, cc3d.density, cc3d.friction, cc3d.restitution);
        b3ShapeId shapeId = b3CreateCapsuleShape(bodyId, &shapeDef, &capsule);
        memcpy(&cc3d.runtimeData, &shapeId, sizeof(b3ShapeId));
    }
    if (entity.hasComponent<MeshCollider3DComponent>()) {
        auto &mc3d = entity.getComponent<MeshCollider3DComponent>();
        b3ShapeId shapeId = b3_nullShapeId;
        runtimeData.meshCook = cookMeshCollider(entity, mc3d);
        if (runtimeData.meshCook) {
            const b3ShapeDef shapeDef = makeShapeDef(mc3d.isSensor, mc3d.density, mc3d.friction, mc3d.restitution);
            shapeId = createMeshColliderShape(
                bodyId, shapeDef, *runtimeData.meshCook, mc3d.convex,
                rb3d.type == Rigidbody3DComponent::BodyType::STATIC, scale, entity
            );
        }
        memcpy(&mc3d.runtimeData, &shapeId, sizeof(b3ShapeId));
    }
    m_bodyEntityMap[bodyId.index1] = entity;
    m_bodies.add(entity, runtimeData);
}

std::shared_ptr<MeshColliderCook> Physics3D::cookMeshCollider(Entity entity, const MeshCollider3DComponent &collider) {
    Physics::MeshColliderSource source;
    if (!Physics::meshColliderSource(entity, collider, source)) {
        warnMeshCollider(entity, "no mesh: the component names none and the entity renders none");
        return nullptr;
    }
    const auto key = std::make_tuple(source.asset, source.submesh, source.primitive);
    const auto it = m_meshCooks.find(key);
    if (it != m_meshCooks.end()) { return it->second; }
    auto cook = std::make_shared<MeshColliderCook>();
    if (!Physics::meshColliderGeometry(source, cook->geometry)) {
        const std::string reason =
            "mesh " + std::to_string((uint32_t)source.asset) + " has no triangles (missing, not a mesh or empty)";
        warnMeshCollider(entity, reason.c_str());
        return nullptr;
    }
    m_meshCooks.emplace(key, cook);
    return cook;
}

void Physics3D::setMeshColliderMesh(Entity entity, AssetId mesh, bool convex) {
    if (!entity.isValid() || !entity.hasComponent<MeshCollider3DComponent>()) { return; }
    MeshCollider3DComponent &collider = entity.getComponent<MeshCollider3DComponent>();
    const bool kindChanged = collider.convex != convex;
    collider.mesh = AssetRef<Asset>(mesh);
    collider.convex = convex;
    if (!isInitialized()) { return; }
    // The triangles are read again: the cook of this source is dropped (the bodies built from the
    // old ones keep theirs), the next cook reads the asset as it is now.
    Physics::MeshColliderSource source;
    if (Physics::meshColliderSource(entity, collider, source)) {
        m_meshCooks.erase(std::make_tuple(source.asset, source.submesh, source.primitive));
    }
    RuntimeBodyData *body = m_bodies.find(entity.getId());
    if (body == nullptr) {
        if (canUse(entity)) { registerEntity(entity); }
        return;
    }
    b3ShapeId shapeId;
    memcpy(&shapeId, &collider.runtimeData, sizeof(b3ShapeId));
    // The shape is replaced in place while its kind stays (a hull for a hull, triangles for
    // triangles): the body and its joints survive. Otherwise, or without a shape to replace, the
    // body is rebuilt as on a property edit.
    std::shared_ptr<MeshColliderCook> cook = nullptr;
    if (!kindChanged && b3Shape_IsValid(shapeId)) { cook = cookMeshCollider(entity, collider); }
    if (!cook) {
        propertiesUpdated(entity);
        return;
    }
    const glm::vec3 scale = glm::abs(entity.calculateWorldSpaceTransform().getScale());
    const b3Vec3 b3Scale = {scale.x, scale.y, scale.z};
    if (convex) {
        b3HullData *hull = cook->convexHull();
        if (hull == nullptr) {
            propertiesUpdated(entity); // the rebuild logs why the collider has no shape
            return;
        }
        // box3d clones the hull into its database: the scaled copy is ours to free.
        b3HullData *scaled = b3CloneAndTransformHull(hull, b3Transform_identity, b3Scale);
        b3Shape_SetHull(shapeId, scaled);
        b3DestroyHull(scaled);
        b3Body_ApplyMassFromShapes(bodyIdOf(*body));
    } else {
        b3MeshData *meshData = cook->triangleMesh();
        if (meshData == nullptr) {
            propertiesUpdated(entity);
            return;
        }
        b3Shape_SetMesh(shapeId, meshData, b3Scale);
    }
    body->meshCook = cook;
}

void Physics3D::componentsUpdated(Entity entity) {
    if (!isInitialized()) { return; }
    const RuntimeBodyData *registered = m_bodies.find(entity.getId());
    if (registered == nullptr) {
        if (canUse(entity)) { registerEntity(entity); }
        return;
    }
    // The body exists and the set of its components has changed. The shapes are created with the
    // body, so it is rebuilt from what the entity has now — where it is and as it moves. Like a
    // property edit, this costs the body its joints.
    const RuntimeBodyData old = *registered;
    const b3BodyId oldBodyId = bodyIdOf(old);
    const b3Pos position = b3Body_GetPosition(oldBodyId);
    const b3Quat rotation = b3Body_GetRotation(oldBodyId);
    const b3Vec3 linearVelocity = b3Body_GetLinearVelocity(oldBodyId);
    const b3Vec3 angularVelocity = b3Body_GetAngularVelocity(oldBodyId);
    removeEntity(entity);
    if (!canUse(entity)) { return; }
    registerEntity(entity);
    RuntimeBodyData &body = *m_bodies.find(entity.getId());
    const b3BodyId bodyId = bodyIdOf(body);
    b3Body_SetTransform(bodyId, position, rotation);
    b3Body_SetLinearVelocity(bodyId, linearVelocity);
    b3Body_SetAngularVelocity(bodyId, angularVelocity);
    body.previousPosition = old.previousPosition;
    body.previousRotation = old.previousRotation;
}

void Physics3D::entityRemoved(Entity entity) {
    if (!isInitialized()) { return; }
    if (isRegistered(entity)) { removeEntity(entity); }
}

void Physics3D::entityRestored(Entity entity) {
    if (!isInitialized()) { return; }
    if (canUse(entity) && !isRegistered(entity)) { registerEntity(entity); }
}

void Physics3D::propertiesUpdated(Panda::Entity entity) {
    if (!isInitialized()) { return; }
    if (!isRegistered(entity)) { return; }
    // The body is recreated instead of a point-by-point mutation: property edits are rare
    // (editor/script), and a box3d hull shape requires a rebuild on a size change anyway.
    removeEntity(entity);
    registerEntity(entity);
}

void Physics3D::syncTransform(Panda::Entity entity) {
    // Body teleport (position/rotation changed from outside: gizmo/script/inspector). NOT through
    // propertiesUpdated: recreating the body would destroy the box3d joints, and a per-frame drag
    // needs a cheap path. The velocities are zeroed ("put it down — it stays", as in 2D), wake —
    // for the body to recompute its contacts.
    if (!isInitialized()) { return; }
    RuntimeBodyData *registered = m_bodies.find(entity.getId());
    if (registered == nullptr) { return; }
    RuntimeBodyData &runtimeData = *registered;
    const b3BodyId bodyId = bodyIdOf(runtimeData);
    auto transform = entity.calculateWorldSpaceTransform();
    b3Body_SetTransform(
        bodyId,
        {transform.getPosition().x, transform.getPosition().y, transform.getPosition().z},
        toB3Quat(transform.getRotation())
    );
    b3Body_SetLinearVelocity(bodyId, {0.f, 0.f, 0.f});
    b3Body_SetAngularVelocity(bodyId, {0.f, 0.f, 0.f});
    b3Body_SetAwake(bodyId, true);
    // A teleport is not a motion: the frame does not draw the way from the old place.
    runtimeData.previousPosition = transform.getPosition();
    runtimeData.previousRotation = transform.getRotation();
}

void Physics3D::removeEntity(Entity entity) {
    if (!isInitialized()) { return; }
    const RuntimeBodyData *registered = m_bodies.find(entity.getId());
    if (registered == nullptr) { return; }
    const b3BodyId bodyId = bodyIdOf(*registered);
    b3DestroyBody(bodyId);
    m_bodyEntityMap.erase(bodyId.index1);
    m_wheelJointMap.erase(entity.getId()); // box3d has destroyed the joint together with the body
    m_bodies.remove(entity);
}

bool Physics3D::findBody(Entity entity, RuntimeBodyData &outBody) {
    if (!isInitialized()) { return false; }
    const RuntimeBodyData *registered = m_bodies.find(entity.getId());
    if (registered == nullptr) { return false; }
    outBody = *registered;
    return true;
}

bool Physics3D::findWheelJoint(Entity entity, RuntimeJointData &outJoint) {
    if (!isInitialized()) { return false; }
    auto it = m_wheelJointMap.find(entity.getId());
    if (it == m_wheelJointMap.end()) { return false; }
    // The joint dies with either of its bodies (the chassis was deleted or recreated).
    if (!b3Joint_IsValid(jointIdOf(it->second))) {
        m_wheelJointMap.erase(it);
        return false;
    }
    outJoint = it->second;
    return true;
}

void Physics3D::applyForce(Entity entity, Vec3 force) {
    RuntimeBodyData body;
    if (!findBody(entity, body)) { return; }
    b3Body_ApplyForceToCenter(bodyIdOf(body), {force.x, force.y, force.z}, true);
}

void Physics3D::applyLinearImpulse(Entity entity, Vec3 impulse) {
    RuntimeBodyData body;
    if (!findBody(entity, body)) { return; }
    b3Body_ApplyLinearImpulseToCenter(bodyIdOf(body), {impulse.x, impulse.y, impulse.z}, true);
}

void Physics3D::setLinearVelocity(Entity entity, Vec3 velocity) {
    RuntimeBodyData body;
    if (!findBody(entity, body)) { return; }
    b3Body_SetLinearVelocity(bodyIdOf(body), {velocity.x, velocity.y, velocity.z});
}

Vec3 Physics3D::getLinearVelocity(Entity entity) {
    RuntimeBodyData body;
    if (!findBody(entity, body)) { return Vec3(); }
    b3Vec3 result = b3Body_GetLinearVelocity(bodyIdOf(body));
    return Vec3(result.x, result.y, result.z);
}

MassData3D Physics3D::getMassData(Entity entity) {
    MassData3D result;
    RuntimeBodyData body;
    if (!findBody(entity, body)) { return result; }
    const b3BodyId bodyId = bodyIdOf(body);
    const b3MassData data = b3Body_GetMassData(bodyId);
    const b3Pos center = b3Body_GetWorldCenterOfMass(bodyId);
    result.mass = data.mass;
    result.center = Vec3((float)center.x, (float)center.y, (float)center.z);
    // box3d keeps the tensor in the body axes. A body with a fixed rotation does not turn at all;
    // the bodies of the world systems have no Rigidbody3D and do not move.
    const b3Matrix3 &local = data.inertia;
    const glm::mat3 inertia(
        local.cx.x, local.cx.y, local.cx.z, local.cy.x, local.cy.y, local.cy.z, local.cz.x, local.cz.y, local.cz.z
    );
    const bool turns =
        entity.hasComponent<Rigidbody3DComponent>() && !entity.getComponent<Rigidbody3DComponent>().fixedRotation;
    if (turns && data.mass > 0.f && glm::determinant(inertia) > 0.f) {
        const b3Quat rotation = b3Body_GetRotation(bodyId);
        const glm::mat3 axes = glm::mat3_cast(glm::quat(rotation.s, rotation.v.x, rotation.v.y, rotation.v.z));
        result.inverseInertia = axes * glm::inverse(inertia) * glm::transpose(axes);
    }
    return result;
}

void Physics3D::applyForceAtPoint(Entity entity, Vec3 force, Vec3 point) {
    RuntimeBodyData body;
    if (!findBody(entity, body)) { return; }
    b3Body_ApplyForce(bodyIdOf(body), {force.x, force.y, force.z}, {point.x, point.y, point.z}, true);
}

void Physics3D::applyLinearImpulseAtPoint(Entity entity, Vec3 impulse, Vec3 point) {
    RuntimeBodyData body;
    if (!findBody(entity, body)) { return; }
    b3Body_ApplyLinearImpulse(bodyIdOf(body), {impulse.x, impulse.y, impulse.z}, {point.x, point.y, point.z}, true);
}

void Physics3D::applyTorque(Entity entity, Vec3 torque) {
    RuntimeBodyData body;
    if (!findBody(entity, body)) { return; }
    b3Body_ApplyTorque(bodyIdOf(body), {torque.x, torque.y, torque.z}, true);
}

void Physics3D::applyAngularImpulse(Entity entity, Vec3 impulse) {
    RuntimeBodyData body;
    if (!findBody(entity, body)) { return; }
    b3Body_ApplyAngularImpulse(bodyIdOf(body), {impulse.x, impulse.y, impulse.z}, true);
}

void Physics3D::setAngularVelocity(Entity entity, Vec3 velocity) {
    RuntimeBodyData body;
    if (!findBody(entity, body)) { return; }
    b3Body_SetAngularVelocity(bodyIdOf(body), {velocity.x, velocity.y, velocity.z});
}

Vec3 Physics3D::getAngularVelocity(Entity entity) {
    RuntimeBodyData body;
    if (!findBody(entity, body)) { return Vec3(); }
    b3Vec3 result = b3Body_GetAngularVelocity(bodyIdOf(body));
    return Vec3(result.x, result.y, result.z);
}

Vec3 Physics3D::getPointVelocity(Entity entity, Vec3 point) {
    RuntimeBodyData body;
    if (!findBody(entity, body)) { return Vec3(); }
    b3Vec3 result = b3Body_GetWorldPointVelocity(bodyIdOf(body), {point.x, point.y, point.z});
    return Vec3(result.x, result.y, result.z);
}

bool Physics3D::raycast(Vec3 origin, Vec3 direction, float maxDistance, Entity ignored, RaycastHit3D &outHit) {
    if (!isInitialized() || maxDistance <= 0.f) { return false; }
    const glm::vec3 ray(direction.x, direction.y, direction.z);
    const float length = glm::length(ray);
    if (length < 1e-6f) { return false; }
    const glm::vec3 translation = ray / length * maxDistance;
    RaycastContext context;
    RuntimeBodyData ignoredBody;
    if (ignored.isValid() && findBody(ignored, ignoredBody)) { context.ignoredBody = bodyIdOf(ignoredBody); }
    b3World_CastRay(
        intToB3WorldId(m_physicsWorldId), {origin.x, origin.y, origin.z}, {translation.x, translation.y, translation.z},
        b3DefaultQueryFilter(), raycastClosestResult, &context
    );
    if (!context.hit) { return false; }
    // Bodies of the world systems are registered with their entities too; a body without an
    // entity still counts as a hit.
    auto it = m_bodyEntityMap.find(b3Shape_GetBody(context.shape).index1);
    outHit.entity = it != m_bodyEntityMap.end() ? it->second : Entity();
    outHit.point = Vec3((float)context.point.x, (float)context.point.y, (float)context.point.z);
    outHit.normal = Vec3(context.normal.x, context.normal.y, context.normal.z);
    outHit.distance = context.fraction * maxDistance;
    outHit.friction = b3Shape_GetFriction(context.shape);
    return true;
}

void Physics3D::setWheelMotor(Entity wheel, bool enabled, float speed, float maxTorque) {
    RuntimeJointData joint;
    if (!findWheelJoint(wheel, joint)) { return; }
    const b3JointId jointId = jointIdOf(joint);
    maxTorque = glm::max(maxTorque, 0.f);
    const bool changed = b3WheelJoint_IsSpinMotorEnabled(jointId) != enabled ||
                         b3WheelJoint_GetSpinMotorSpeed(jointId) != speed ||
                         b3WheelJoint_GetMaxSpinTorque(jointId) != maxTorque;
    if (changed) {
        b3WheelJoint_EnableSpinMotor(jointId, enabled);
        b3WheelJoint_SetSpinMotorSpeed(jointId, speed);
        b3WheelJoint_SetMaxSpinTorque(jointId, maxTorque);
    }
    // The joint setters do not wake the bodies. A motor that drives keeps them awake: a car that
    // pushes slowly against an obstacle must not fall asleep under the throttle. A brake and a
    // released motor wake them only on a change, so a parked car sleeps.
    const bool driving = enabled && speed != 0.f && maxTorque > 0.f;
    if (changed || driving) { b3Joint_WakeBodies(jointId); }
}

float Physics3D::getWheelSpinSpeed(Entity wheel) {
    RuntimeJointData joint;
    if (!findWheelJoint(wheel, joint)) { return 0.f; }
    return b3WheelJoint_GetSpinSpeed(jointIdOf(joint));
}

void Physics3D::setWheelSteeringAngle(Entity wheel, float angle) {
    RuntimeJointData joint;
    if (!findWheelJoint(wheel, joint)) { return; }
    const b3JointId jointId = jointIdOf(joint);
    if (b3WheelJoint_GetTargetSteeringAngle(jointId) == angle) { return; }
    b3WheelJoint_SetTargetSteeringAngle(jointId, angle);
    b3Joint_WakeBodies(jointId);
}

float Physics3D::getWheelSteeringAngle(Entity wheel) {
    RuntimeJointData joint;
    if (!findWheelJoint(wheel, joint)) { return 0.f; }
    return b3WheelJoint_GetSteeringAngle(jointIdOf(joint));
}

void Physics3D::setWheelSuspension(Entity wheel, float hertz, float dampingRatio) {
    RuntimeJointData joint;
    if (!findWheelJoint(wheel, joint)) { return; }
    const b3JointId jointId = jointIdOf(joint);
    hertz = glm::max(hertz, 0.f);
    dampingRatio = glm::max(dampingRatio, 0.f);
    if (b3WheelJoint_GetSuspensionHertz(jointId) == hertz &&
        b3WheelJoint_GetSuspensionDampingRatio(jointId) == dampingRatio) {
        return;
    }
    b3WheelJoint_SetSuspensionHertz(jointId, hertz);
    b3WheelJoint_SetSuspensionDampingRatio(jointId, dampingRatio);
    b3Joint_WakeBodies(jointId);
}

CharacterMoveResult Physics3D::moveCharacter(Entity entity, Vec3 translation, Vec3 velocity, float pushForce) {
    CharacterMoveResult result;
    result.translation = translation;
    result.velocity = velocity;
    RuntimeBodyData *registered = isInitialized() ? m_bodies.find(entity.getId()) : nullptr;
    if (registered == nullptr || !entity.hasComponent<CapsuleCollider3DComponent>()) {
        return result; // no registered capsule, nothing to move with — return the input as is
    }
    b3WorldId worldId = intToB3WorldId(m_physicsWorldId);
    RuntimeBodyData &runtimeData = *registered;
    const b3BodyId bodyId = bodyIdOf(runtimeData);

    // The mover capsule — the same math as at the shape registration (upright, Y axis, the body
    // rotation is not taken into account).
    auto &cc3d = entity.getComponent<CapsuleCollider3DComponent>();
    auto transform = entity.calculateWorldSpaceTransform();
    const glm::vec3 scale = glm::abs(transform.getScale());
    const float halfHeight = glm::abs(cc3d.halfHeight) * scale.y;
    b3Capsule capsule;
    capsule.center1 = {cc3d.offset.x, cc3d.offset.y - halfHeight, cc3d.offset.z};
    capsule.center2 = {cc3d.offset.x, cc3d.offset.y + halfHeight, cc3d.offset.z};
    capsule.radius = glm::abs(cc3d.radius) * glm::max(scale.x, scale.z);

    const b3Pos origin = b3Body_GetPosition(bodyId);
    const b3QueryFilter filter = b3DefaultQueryFilter();
    MoverContext context;
    context.selfBody = bodyId;

    // 1) Capsule cast: the safe fraction of the desired displacement (anti-tunneling).
    const b3Vec3 delta = {translation.x, translation.y, translation.z};
    const float fraction = b3World_CastMover(worldId, origin, &capsule, delta, filter, moverShapeFilter, &context);
    b3Pos position = {origin.x + fraction * delta.x, origin.y + fraction * delta.y, origin.z + fraction * delta.z};

    // 2) Contact planes at the reached position; 3) the solver eats up the remainder with
    // sliding/push-out.
    b3World_CollideMover(worldId, position, &capsule, filter, moverPlaneResult, &context);
    const b3Pos collideOrigin = position; // the contact points are given relative to this position
    const b3Vec3 remaining = {(1.f - fraction) * delta.x, (1.f - fraction) * delta.y, (1.f - fraction) * delta.z};
    const b3PlaneSolverResult solved = b3SolvePlanes(remaining, context.planes, context.planeCount);
    position = {position.x + solved.delta.x, position.y + solved.delta.y, position.z + solved.delta.z};

    // Pushing dynamic bodies: a force at the contact point along -normal, the scale is how far the
    // character has actually pressed into the soft plane (plane.push from the solver); a touch
    // without pressure gives no force.
    if (pushForce > 0.f) {
        for (int i = 0; i < context.dynamicContactCount; ++i) {
            const MoverDynamicContact &contact = context.dynamicContacts[i];
            const float depth = context.planes[contact.planeIndex].push / MOVER_DYNAMIC_PUSH_LIMIT;
            if (depth <= 0.f) { continue; }
            const float magnitude = pushForce * glm::min(depth, 1.f);
            const b3Vec3 force = {
                -contact.normal.x * magnitude, -contact.normal.y * magnitude, -contact.normal.z * magnitude};
            const b3Pos point = {
                collideOrigin.x + contact.point.x,
                collideOrigin.y + contact.point.y,
                collideOrigin.z + contact.point.z};
            b3Body_ApplyForce(contact.body, force, point, true);
        }
    }

    const b3Quat rotation = b3Body_GetRotation(bodyId);
    b3Body_SetTransform(bodyId, position, rotation);
    // The entity transform — right away, without waiting for the physics step.
    const glm::vec3 moved((float)position.x, (float)position.y, (float)position.z);
    entity.getWorld()->setWorldPose(entity, moved, {rotation.s, rotation.v.x, rotation.v.y, rotation.v.z});
    // Inside a step the move is a part of the step's motion and is drawn between its two poses.
    // Outside — nothing draws the way to the new place.
    if (!m_stepping) { runtimeData.previousPosition = moved; }

    const b3Vec3 clipped =
        b3ClipVector({velocity.x, velocity.y, velocity.z}, context.planes, context.planeCount);
    result.translation =
        Vec3((float)(position.x - origin.x), (float)(position.y - origin.y), (float)(position.z - origin.z));
    result.velocity = Vec3(clipped.x, clipped.y, clipped.z);
    result.grounded = context.grounded;
    return result;
}

float Physics3D::getMass(Entity entity) {
    RuntimeBodyData body;
    if (!findBody(entity, body)) { return 0; }
    return b3Body_GetMass(bodyIdOf(body));
}

void Physics3D::shutdown() {
    if (m_physicsWorldId) {
        b3WorldId id = intToB3WorldId(m_physicsWorldId);
        b3DestroyWorld(id);
        m_physicsWorldId = 0;
    }
    m_bodyEntityMap.clear();
    m_bodies.clear();
    m_meshCooks.clear(); // after the world: the mesh shapes referenced the cooked data
    m_wheelJointMap.clear();
    m_writePass = 0;
    m_stepping = false;
    m_world = nullptr;
}

bool Physics3D::isRegistered(Entity entity) {
    return m_bodies.contains(entity.getId());
}

// A body is for an entity the world sees: a deleted one (the tombstone) has none.
bool Physics3D::canUse(Entity entity) {
    return entity.isValid() && entity.hasComponent<Rigidbody3DComponent>() &&
           (entity.hasComponent<BoxCollider3DComponent>() || entity.hasComponent<SphereCollider3DComponent>() ||
            entity.hasComponent<CapsuleCollider3DComponent>() || entity.hasComponent<MeshCollider3DComponent>());
}

} // namespace Panda
