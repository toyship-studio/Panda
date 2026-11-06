#include "Panda/PhysicsEditor/PhysicsEditorExtensions.hpp"

#include "Panda/PhysicsEditor/ColliderLines.hpp"
#include "Panda/PhysicsEditor/PhysicsEditorSettings.hpp"

#include <PandaEditorKit/ComponentCommands.hpp>
#include <PandaEditorKit/EditorExtensions.hpp>
#include <PandaEditorKit/EditorHost.hpp>
#include <PandaEditorKit/SettingsCommands.hpp>
#include <PandaEditorKit/Widgets/FormWidgets.hpp>

#include <Panda/Extensions/Extensions.hpp>
#include <Panda/GameLogic/GameContext.hpp>
#include <Panda/GameLogic/World.hpp>
#include <Panda/Physics/Physics2D.hpp>
#include <Panda/Physics/Physics3D.hpp>
#include <Panda/Physics/PhysicsComponents.hpp>

#include <memory>
#include <unordered_set>

namespace PhysicsEditor {

using namespace Editor;
using namespace PandaUI;

namespace {

    // ---- Inspector sections. A section edits a copy of the component and writes it to every
    // target with one undo command (Kit: commitComponent<T>). While the world runs, the edit
    // reaches the simulation: the body of a target is rebuilt from the new values (at the
    // entity's transform, at rest; its joints are made again), a joint is made again from its
    // component (at the relative pose of the moment).

    enum class Rebuild { None, Body, Joint };

    template<typename T>
    struct Section {
        CollapsibleSection &section;
        Panda::World &world;
        const std::vector<Panda::Entity> &entities;
        std::shared_ptr<T> data;

        explicit Section(CollapsibleSection &section, const ComponentInspectorContext &context)
            : section(section)
            , world(context.world)
            , entities(context.entities)
            , data(std::make_shared<T>(context.entities.front().getComponent<T>())) {}

        std::function<void()> emitter(Rebuild rebuild) const {
            Panda::World *world = &this->world;
            const std::vector<Panda::Entity> entities = this->entities;
            std::shared_ptr<T> data = this->data;
            return [world, entities, data, rebuild] {
                commitComponent<T>(*world, entities, *data);
                if (rebuild == Rebuild::None || !world->isRunning()) { return; }
                Panda::Physics2D *physics2D = world->findContext<Panda::Physics2D>();
                Panda::Physics3D *physics3D = world->findContext<Panda::Physics3D>();
                for (Panda::Entity entity : entities) {
                    if (!entity.hasComponent<T>()) { continue; }
                    if (rebuild == Rebuild::Body) {
                        if (physics2D) { physics2D->propertiesUpdated(entity); }
                        if (physics3D) { physics3D->propertiesUpdated(entity); }
                    } else {
                        if (physics2D) { physics2D->jointComponentsUpdated(entity); }
                        if (physics3D) { physics3D->jointComponentsUpdated(entity); }
                    }
                }
            };
        }

        template<typename Get>
        bool mixed(Get get) const {
            return componentFieldMixed<T>(entities, get);
        }
        template<typename Get>
        glm::bvec3 vectorMixed(Get get) const {
            return componentVectorMixed<T>(entities, get);
        }
    };

    template<typename T>
    void setPresence(Panda::World &world, const std::vector<Panda::Entity> &entities, bool add) {
        toggleComponentPresence<T>(world, std::unordered_set<Panda::Entity>(entities.begin(), entities.end()), add);
    }

    // --- Bodies.

    template<typename T>
    void buildRigidbodySection(CollapsibleSection &section, const ComponentInspectorContext &context) {
        Section<T> form(section, context);
        auto data = form.data;
        auto emit = form.emitter(Rebuild::Body);
        addComboRow(
            section, "Body Type", {"Static", "Dynamic", "Kinematic"}, static_cast<int>(data->type),
            [data, emit](int index) {
                data->type = static_cast<typename T::BodyType>(index);
                emit();
            },
            form.mixed([](T &c) { return static_cast<int>(c.type); })
        );
        addToggleRow(
            section, "Fixed Rotation", data->fixedRotation,
            [data, emit](bool on) {
                data->fixedRotation = on;
                emit();
            },
            form.mixed([](T &c) { return c.fixedRotation; })
        );
    }

    // --- Colliders: the shape, then the material of the shape.

    template<typename T>
    void addColliderMaterialRows(Section<T> &form, std::function<void()> emit, float maxDensity) {
        auto data = form.data;
        addNumberRow(
            form.section, "Density", 0.f, maxDensity, data->density,
            [data, emit](float v) {
                data->density = v;
                emit();
            },
            form.mixed([](T &c) { return c.density; })
        );
        addNumberRow(
            form.section, "Friction", 0.f, 1.f, data->friction,
            [data, emit](float v) {
                data->friction = v;
                emit();
            },
            form.mixed([](T &c) { return c.friction; })
        );
        addNumberRow(
            form.section, "Restitution", 0.f, 1.f, data->restitution,
            [data, emit](float v) {
                data->restitution = v;
                emit();
            },
            form.mixed([](T &c) { return c.restitution; })
        );
    }

    template<typename T>
    void addSensorRow(Section<T> &form, std::function<void()> emit) {
        auto data = form.data;
        addToggleRow(
            form.section, "Is Sensor", data->isSensor,
            [data, emit](bool on) {
                data->isSensor = on;
                emit();
            },
            form.mixed([](T &c) { return c.isSensor; })
        );
    }

    void buildBoxCollider2DSection(CollapsibleSection &section, const ComponentInspectorContext &context) {
        using T = Panda::BoxCollider2DComponent;
        Section<T> form(section, context);
        auto data = form.data;
        auto emit = form.emitter(Rebuild::Body);
        addSensorRow(form, emit);
        addNumberRow(section, "Offset X", -1000.f, 1000.f, data->offset.x, [data, emit](float v) { data->offset.x = v; emit(); },
                     form.mixed([](T &c) { return c.offset.x; }));
        addNumberRow(section, "Offset Y", -1000.f, 1000.f, data->offset.y, [data, emit](float v) { data->offset.y = v; emit(); },
                     form.mixed([](T &c) { return c.offset.y; }));
        addNumberRow(section, "Size X", 0.f, 1000.f, data->size.x, [data, emit](float v) { data->size.x = v; emit(); },
                     form.mixed([](T &c) { return c.size.x; }));
        addNumberRow(section, "Size Y", 0.f, 1000.f, data->size.y, [data, emit](float v) { data->size.y = v; emit(); },
                     form.mixed([](T &c) { return c.size.y; }));
        addColliderMaterialRows(form, emit, 1.f);
    }

    void buildBoxCollider3DSection(CollapsibleSection &section, const ComponentInspectorContext &context) {
        using T = Panda::BoxCollider3DComponent;
        Section<T> form(section, context);
        auto data = form.data;
        auto emit = form.emitter(Rebuild::Body);
        addSensorRow(form, emit);
        addVector3Row(section, "Offset", data->offset, [data, emit](glm::vec3 v) { data->offset = v; emit(); },
                      form.vectorMixed([](T &c) { return c.offset; }));
        addVector3Row(section, "Size", data->size, [data, emit](glm::vec3 v) { data->size = v; emit(); },
                      form.vectorMixed([](T &c) { return c.size; }));
        addColliderMaterialRows(form, emit, 1000.f);
    }

    void buildSphereCollider3DSection(CollapsibleSection &section, const ComponentInspectorContext &context) {
        using T = Panda::SphereCollider3DComponent;
        Section<T> form(section, context);
        auto data = form.data;
        auto emit = form.emitter(Rebuild::Body);
        addSensorRow(form, emit);
        addVector3Row(section, "Offset", data->offset, [data, emit](glm::vec3 v) { data->offset = v; emit(); },
                      form.vectorMixed([](T &c) { return c.offset; }));
        addNumberRow(section, "Radius", 0.001f, 1000.f, data->radius, [data, emit](float v) { data->radius = v; emit(); },
                     form.mixed([](T &c) { return c.radius; }));
        addColliderMaterialRows(form, emit, 1000.f);
    }

    void buildCapsuleCollider3DSection(CollapsibleSection &section, const ComponentInspectorContext &context) {
        using T = Panda::CapsuleCollider3DComponent;
        Section<T> form(section, context);
        auto data = form.data;
        auto emit = form.emitter(Rebuild::Body);
        addSensorRow(form, emit);
        addVector3Row(section, "Offset", data->offset, [data, emit](glm::vec3 v) { data->offset = v; emit(); },
                      form.vectorMixed([](T &c) { return c.offset; }));
        addNumberRow(section, "Radius", 0.001f, 1000.f, data->radius, [data, emit](float v) { data->radius = v; emit(); },
                     form.mixed([](T &c) { return c.radius; }));
        addNumberRow(section, "Half Height", 0.f, 1000.f, data->halfHeight, [data, emit](float v) { data->halfHeight = v; emit(); },
                     form.mixed([](T &c) { return c.halfHeight; }));
        addColliderMaterialRows(form, emit, 1000.f);
    }

    void buildMeshCollider3DSection(CollapsibleSection &section, const ComponentInspectorContext &context) {
        using T = Panda::MeshCollider3DComponent;
        Section<T> form(section, context);
        auto data = form.data;
        auto emit = form.emitter(Rebuild::Body);
        // The mesh: an imported mesh of the project; empty — the mesh the entity itself renders.
        addAssetRow(section, "Mesh", "mesh", static_cast<uint32_t>(data->mesh.getId()), [data, emit](uint32_t id) {
            data->mesh = Panda::AssetRef<Panda::Asset>(Panda::AssetId(id));
            emit();
        });
        addToggleRow(
            section, "Convex", data->convex,
            [data, emit](bool on) {
                data->convex = on;
                emit();
            },
            form.mixed([](T &c) { return c.convex; })
        );
        section.addContent(makeFormHint(
            "Empty mesh: the one the entity renders. Convex: the hull of the mesh, for dynamic and kinematic "
            "bodies. Not convex: the triangles themselves, static bodies only."
        ));
        addSensorRow(form, emit);
        addColliderMaterialRows(form, emit, 1000.f);
    }

    // --- Joints: the connected entity, the geometry, the limits and the motor.

    template<typename T>
    void addConnectedRow(Section<T> &form, std::function<void()> emit, const char *title) {
        auto data = form.data;
        addEntityRow(
            form.section, title, form.world, data->connectedEntity,
            [data, emit](Panda::UUID id) {
                data->connectedEntity = id;
                emit();
            },
            form.mixed([](T &c) { return c.connectedEntity; })
        );
    }

    // Whether the two bodies of the joint still collide with each other (off by default).
    template<typename T>
    void addCollideConnectedRow(Section<T> &form, std::function<void()> emit) {
        auto data = form.data;
        addToggleRow(
            form.section, "Collide Connected", data->collideConnected,
            [data, emit](bool v) {
                data->collideConnected = v;
                emit();
            },
            form.mixed([](T &c) { return c.collideConnected; })
        );
    }

    template<typename T>
    void addMotorRows(Section<T> &form, std::function<void()> emit, float maxSpeed, float maxTorque) {
        auto data = form.data;
        addToggleRow(form.section, "Enable Motor", data->enableMotor, [data, emit](bool v) { data->enableMotor = v; emit(); },
                     form.mixed([](T &c) { return c.enableMotor; }));
        addNumberRow(form.section, "Motor Speed", -maxSpeed, maxSpeed, data->motorSpeed,
                     [data, emit](float v) { data->motorSpeed = v; emit(); }, form.mixed([](T &c) { return c.motorSpeed; }));
        addNumberRow(form.section, "Max Torque", 0.f, maxTorque, data->maxMotorTorque,
                     [data, emit](float v) { data->maxMotorTorque = v; emit(); },
                     form.mixed([](T &c) { return c.maxMotorTorque; }));
    }

    template<typename T>
    void addAngleLimitRows(Section<T> &form, std::function<void()> emit) {
        auto data = form.data;
        addToggleRow(form.section, "Enable Limit", data->enableLimit, [data, emit](bool v) { data->enableLimit = v; emit(); },
                     form.mixed([](T &c) { return c.enableLimit; }));
        addNumberRow(form.section, "Lower Angle", -178.f, 178.f, data->lowerAngle,
                     [data, emit](float v) { data->lowerAngle = v; emit(); }, form.mixed([](T &c) { return c.lowerAngle; }));
        addNumberRow(form.section, "Upper Angle", -178.f, 178.f, data->upperAngle,
                     [data, emit](float v) { data->upperAngle = v; emit(); }, form.mixed([](T &c) { return c.upperAngle; }));
    }

    void buildRevoluteJoint2DSection(CollapsibleSection &section, const ComponentInspectorContext &context) {
        using T = Panda::RevoluteJoint2DComponent;
        Section<T> form(section, context);
        auto data = form.data;
        auto emit = form.emitter(Rebuild::Joint);
        addConnectedRow(form, emit, "Connected");
        addCollideConnectedRow(form, emit);
        addNumberRow(section, "Anchor X", -1000.f, 1000.f, data->anchor.x, [data, emit](float v) { data->anchor.x = v; emit(); },
                     form.mixed([](T &c) { return c.anchor.x; }));
        addNumberRow(section, "Anchor Y", -1000.f, 1000.f, data->anchor.y, [data, emit](float v) { data->anchor.y = v; emit(); },
                     form.mixed([](T &c) { return c.anchor.y; }));
        addAngleLimitRows(form, emit);
        addMotorRows(form, emit, 3600.f, 100000.f);
    }

    void buildDistanceJoint2DSection(CollapsibleSection &section, const ComponentInspectorContext &context) {
        using T = Panda::DistanceJoint2DComponent;
        Section<T> form(section, context);
        auto data = form.data;
        auto emit = form.emitter(Rebuild::Joint);
        addConnectedRow(form, emit, "Connected");
        addCollideConnectedRow(form, emit);
        addNumberRow(section, "Anchor X", -1000.f, 1000.f, data->anchor.x, [data, emit](float v) { data->anchor.x = v; emit(); },
                     form.mixed([](T &c) { return c.anchor.x; }));
        addNumberRow(section, "Anchor Y", -1000.f, 1000.f, data->anchor.y, [data, emit](float v) { data->anchor.y = v; emit(); },
                     form.mixed([](T &c) { return c.anchor.y; }));
        addNumberRow(section, "Connected Anchor X", -1000.f, 1000.f, data->connectedAnchor.x,
                     [data, emit](float v) { data->connectedAnchor.x = v; emit(); },
                     form.mixed([](T &c) { return c.connectedAnchor.x; }));
        addNumberRow(section, "Connected Anchor Y", -1000.f, 1000.f, data->connectedAnchor.y,
                     [data, emit](float v) { data->connectedAnchor.y = v; emit(); },
                     form.mixed([](T &c) { return c.connectedAnchor.y; }));
        addNumberRow(section, "Length (0 = auto)", 0.f, 10000.f, data->length, [data, emit](float v) { data->length = v; emit(); },
                     form.mixed([](T &c) { return c.length; }));
        addToggleRow(section, "Enable Spring", data->enableSpring, [data, emit](bool v) { data->enableSpring = v; emit(); },
                     form.mixed([](T &c) { return c.enableSpring; }));
        addNumberRow(section, "Hertz", 0.f, 60.f, data->hertz, [data, emit](float v) { data->hertz = v; emit(); },
                     form.mixed([](T &c) { return c.hertz; }));
        addNumberRow(section, "Damping", 0.f, 10.f, data->dampingRatio, [data, emit](float v) { data->dampingRatio = v; emit(); },
                     form.mixed([](T &c) { return c.dampingRatio; }));
    }

    void buildRevoluteJoint3DSection(CollapsibleSection &section, const ComponentInspectorContext &context) {
        using T = Panda::RevoluteJoint3DComponent;
        Section<T> form(section, context);
        auto data = form.data;
        auto emit = form.emitter(Rebuild::Joint);
        addConnectedRow(form, emit, "Connected");
        addCollideConnectedRow(form, emit);
        addVector3Row(section, "Anchor", data->anchor, [data, emit](glm::vec3 v) { data->anchor = v; emit(); },
                      form.vectorMixed([](T &c) { return c.anchor; }));
        addVector3Row(section, "Axis", data->axis, [data, emit](glm::vec3 v) { data->axis = v; emit(); },
                      form.vectorMixed([](T &c) { return c.axis; }));
        addAngleLimitRows(form, emit);
        addMotorRows(form, emit, 3600.f, 100000.f);
    }

    void buildSphericalJoint3DSection(CollapsibleSection &section, const ComponentInspectorContext &context) {
        using T = Panda::SphericalJoint3DComponent;
        Section<T> form(section, context);
        auto data = form.data;
        auto emit = form.emitter(Rebuild::Joint);
        addConnectedRow(form, emit, "Connected");
        addCollideConnectedRow(form, emit);
        addVector3Row(section, "Anchor", data->anchor, [data, emit](glm::vec3 v) { data->anchor = v; emit(); },
                      form.vectorMixed([](T &c) { return c.anchor; }));
        addVector3Row(section, "Axis", data->axis, [data, emit](glm::vec3 v) { data->axis = v; emit(); },
                      form.vectorMixed([](T &c) { return c.axis; }));
        addToggleRow(section, "Enable Cone Limit", data->enableConeLimit,
                     [data, emit](bool v) { data->enableConeLimit = v; emit(); },
                     form.mixed([](T &c) { return c.enableConeLimit; }));
        addNumberRow(section, "Cone Angle", 0.f, 180.f, data->coneAngle, [data, emit](float v) { data->coneAngle = v; emit(); },
                     form.mixed([](T &c) { return c.coneAngle; }));
        addToggleRow(section, "Enable Twist Limit", data->enableTwistLimit,
                     [data, emit](bool v) { data->enableTwistLimit = v; emit(); },
                     form.mixed([](T &c) { return c.enableTwistLimit; }));
        addNumberRow(section, "Lower Twist", -178.f, 178.f, data->lowerTwistAngle,
                     [data, emit](float v) { data->lowerTwistAngle = v; emit(); },
                     form.mixed([](T &c) { return c.lowerTwistAngle; }));
        addNumberRow(section, "Upper Twist", -178.f, 178.f, data->upperTwistAngle,
                     [data, emit](float v) { data->upperTwistAngle = v; emit(); },
                     form.mixed([](T &c) { return c.upperTwistAngle; }));
    }

    void buildWheelJoint3DSection(CollapsibleSection &section, const ComponentInspectorContext &context) {
        using T = Panda::WheelJoint3DComponent;
        Section<T> form(section, context);
        auto data = form.data;
        auto emit = form.emitter(Rebuild::Joint);
        // The joint sits on the wheel: the connected entity is the chassis.
        addConnectedRow(form, emit, "Chassis");
        addCollideConnectedRow(form, emit);
        addVector3Row(section, "Anchor", data->anchor, [data, emit](glm::vec3 v) { data->anchor = v; emit(); },
                      form.vectorMixed([](T &c) { return c.anchor; }));
        addVector3Row(section, "Spin Axis", data->axis, [data, emit](glm::vec3 v) { data->axis = v; emit(); },
                      form.vectorMixed([](T &c) { return c.axis; }));
        addVector3Row(section, "Suspension Axis", data->suspensionAxis,
                      [data, emit](glm::vec3 v) { data->suspensionAxis = v; emit(); },
                      form.vectorMixed([](T &c) { return c.suspensionAxis; }));
        addToggleRow(section, "Enable Spring", data->enableSuspensionSpring,
                     [data, emit](bool v) { data->enableSuspensionSpring = v; emit(); },
                     form.mixed([](T &c) { return c.enableSuspensionSpring; }));
        addNumberRow(section, "Spring Hertz", 0.f, 60.f, data->suspensionHertz,
                     [data, emit](float v) { data->suspensionHertz = v; emit(); },
                     form.mixed([](T &c) { return c.suspensionHertz; }));
        addNumberRow(section, "Spring Damping", 0.f, 10.f, data->suspensionDampingRatio,
                     [data, emit](float v) { data->suspensionDampingRatio = v; emit(); },
                     form.mixed([](T &c) { return c.suspensionDampingRatio; }));
        addToggleRow(section, "Enable Travel Limit", data->enableSuspensionLimit,
                     [data, emit](bool v) { data->enableSuspensionLimit = v; emit(); },
                     form.mixed([](T &c) { return c.enableSuspensionLimit; }));
        addNumberRow(section, "Lower Travel", -10.f, 10.f, data->lowerSuspensionLimit,
                     [data, emit](float v) { data->lowerSuspensionLimit = v; emit(); },
                     form.mixed([](T &c) { return c.lowerSuspensionLimit; }));
        addNumberRow(section, "Upper Travel", -10.f, 10.f, data->upperSuspensionLimit,
                     [data, emit](float v) { data->upperSuspensionLimit = v; emit(); },
                     form.mixed([](T &c) { return c.upperSuspensionLimit; }));
        addToggleRow(section, "Enable Steering", data->enableSteering, [data, emit](bool v) { data->enableSteering = v; emit(); },
                     form.mixed([](T &c) { return c.enableSteering; }));
        addNumberRow(section, "Steering Hertz", 0.f, 60.f, data->steeringHertz,
                     [data, emit](float v) { data->steeringHertz = v; emit(); },
                     form.mixed([](T &c) { return c.steeringHertz; }));
        addNumberRow(section, "Steering Damping", 0.f, 10.f, data->steeringDampingRatio,
                     [data, emit](float v) { data->steeringDampingRatio = v; emit(); },
                     form.mixed([](T &c) { return c.steeringDampingRatio; }));
        addNumberRow(section, "Max Steering Torque", 0.f, 1000000.f, data->maxSteeringTorque,
                     [data, emit](float v) { data->maxSteeringTorque = v; emit(); },
                     form.mixed([](T &c) { return c.maxSteeringTorque; }));
        addToggleRow(section, "Enable Steering Limit", data->enableSteeringLimit,
                     [data, emit](bool v) { data->enableSteeringLimit = v; emit(); },
                     form.mixed([](T &c) { return c.enableSteeringLimit; }));
        addNumberRow(section, "Lower Steering Angle", -89.f, 89.f, data->lowerSteeringAngle,
                     [data, emit](float v) { data->lowerSteeringAngle = v; emit(); },
                     form.mixed([](T &c) { return c.lowerSteeringAngle; }));
        addNumberRow(section, "Upper Steering Angle", -89.f, 89.f, data->upperSteeringAngle,
                     [data, emit](float v) { data->upperSteeringAngle = v; emit(); },
                     form.mixed([](T &c) { return c.upperSteeringAngle; }));
        addMotorRows(form, emit, 100000.f, 1000000.f);
    }

    void buildWeldJoint3DSection(CollapsibleSection &section, const ComponentInspectorContext &context) {
        using T = Panda::WeldJoint3DComponent;
        Section<T> form(section, context);
        auto data = form.data;
        auto emit = form.emitter(Rebuild::Joint);
        addConnectedRow(form, emit, "Connected");
        addCollideConnectedRow(form, emit);
        addVector3Row(section, "Anchor", data->anchor, [data, emit](glm::vec3 v) { data->anchor = v; emit(); },
                      form.vectorMixed([](T &c) { return c.anchor; }));
        section.addContent(makeFormHint("Zero stiffness is rigid; a positive hertz makes the weld a spring."));
        addNumberRow(section, "Linear Hertz", 0.f, 60.f, data->linearHertz, [data, emit](float v) { data->linearHertz = v; emit(); },
                     form.mixed([](T &c) { return c.linearHertz; }));
        addNumberRow(section, "Linear Damping", 0.f, 10.f, data->linearDampingRatio,
                     [data, emit](float v) { data->linearDampingRatio = v; emit(); },
                     form.mixed([](T &c) { return c.linearDampingRatio; }));
        addNumberRow(section, "Angular Hertz", 0.f, 60.f, data->angularHertz,
                     [data, emit](float v) { data->angularHertz = v; emit(); }, form.mixed([](T &c) { return c.angularHertz; }));
        addNumberRow(section, "Angular Damping", 0.f, 10.f, data->angularDampingRatio,
                     [data, emit](float v) { data->angularDampingRatio = v; emit(); },
                     form.mixed([](T &c) { return c.angularDampingRatio; }));
    }

    void buildPrismaticJoint3DSection(CollapsibleSection &section, const ComponentInspectorContext &context) {
        using T = Panda::PrismaticJoint3DComponent;
        Section<T> form(section, context);
        auto data = form.data;
        auto emit = form.emitter(Rebuild::Joint);
        addConnectedRow(form, emit, "Connected");
        addCollideConnectedRow(form, emit);
        addVector3Row(section, "Anchor", data->anchor, [data, emit](glm::vec3 v) { data->anchor = v; emit(); },
                      form.vectorMixed([](T &c) { return c.anchor; }));
        addVector3Row(section, "Axis", data->axis, [data, emit](glm::vec3 v) { data->axis = v; emit(); },
                      form.vectorMixed([](T &c) { return c.axis; }));
        addToggleRow(section, "Enable Spring", data->enableSpring, [data, emit](bool v) { data->enableSpring = v; emit(); },
                     form.mixed([](T &c) { return c.enableSpring; }));
        addNumberRow(section, "Hertz", 0.f, 60.f, data->hertz, [data, emit](float v) { data->hertz = v; emit(); },
                     form.mixed([](T &c) { return c.hertz; }));
        addNumberRow(section, "Damping", 0.f, 10.f, data->dampingRatio, [data, emit](float v) { data->dampingRatio = v; emit(); },
                     form.mixed([](T &c) { return c.dampingRatio; }));
        addNumberRow(section, "Target Translation", -100.f, 100.f, data->targetTranslation,
                     [data, emit](float v) { data->targetTranslation = v; emit(); },
                     form.mixed([](T &c) { return c.targetTranslation; }));
        addToggleRow(section, "Enable Limit", data->enableLimit, [data, emit](bool v) { data->enableLimit = v; emit(); },
                     form.mixed([](T &c) { return c.enableLimit; }));
        addNumberRow(section, "Lower Translation", -100.f, 100.f, data->lowerTranslation,
                     [data, emit](float v) { data->lowerTranslation = v; emit(); },
                     form.mixed([](T &c) { return c.lowerTranslation; }));
        addNumberRow(section, "Upper Translation", -100.f, 100.f, data->upperTranslation,
                     [data, emit](float v) { data->upperTranslation = v; emit(); },
                     form.mixed([](T &c) { return c.upperTranslation; }));
        addToggleRow(section, "Enable Motor", data->enableMotor, [data, emit](bool v) { data->enableMotor = v; emit(); },
                     form.mixed([](T &c) { return c.enableMotor; }));
        addNumberRow(section, "Motor Speed", -100.f, 100.f, data->motorSpeed, [data, emit](float v) { data->motorSpeed = v; emit(); },
                     form.mixed([](T &c) { return c.motorSpeed; }));
        addNumberRow(section, "Max Force", 0.f, 1000000.f, data->maxMotorForce,
                     [data, emit](float v) { data->maxMotorForce = v; emit(); }, form.mixed([](T &c) { return c.maxMotorForce; }));
    }

    void buildDistanceJoint3DSection(CollapsibleSection &section, const ComponentInspectorContext &context) {
        using T = Panda::DistanceJoint3DComponent;
        Section<T> form(section, context);
        auto data = form.data;
        auto emit = form.emitter(Rebuild::Joint);
        addConnectedRow(form, emit, "Connected");
        addVector3Row(section, "Anchor", data->anchor, [data, emit](glm::vec3 v) { data->anchor = v; emit(); },
                      form.vectorMixed([](T &c) { return c.anchor; }));
        addVector3Row(section, "Connected Anchor", data->connectedAnchor,
                      [data, emit](glm::vec3 v) { data->connectedAnchor = v; emit(); },
                      form.vectorMixed([](T &c) { return c.connectedAnchor; }));
        addNumberRow(section, "Length (0 = auto)", 0.f, 10000.f, data->length, [data, emit](float v) { data->length = v; emit(); },
                     form.mixed([](T &c) { return c.length; }));
        section.addContent(makeFormHint("Rigid without the spring; the limit and the motor apply with it."));
        addToggleRow(section, "Enable Spring", data->enableSpring, [data, emit](bool v) { data->enableSpring = v; emit(); },
                     form.mixed([](T &c) { return c.enableSpring; }));
        addNumberRow(section, "Hertz", 0.f, 60.f, data->hertz, [data, emit](float v) { data->hertz = v; emit(); },
                     form.mixed([](T &c) { return c.hertz; }));
        addNumberRow(section, "Damping", 0.f, 10.f, data->dampingRatio, [data, emit](float v) { data->dampingRatio = v; emit(); },
                     form.mixed([](T &c) { return c.dampingRatio; }));
        addToggleRow(section, "Enable Limit", data->enableLimit, [data, emit](bool v) { data->enableLimit = v; emit(); },
                     form.mixed([](T &c) { return c.enableLimit; }));
        addNumberRow(section, "Min Length", 0.f, 10000.f, data->minLength, [data, emit](float v) { data->minLength = v; emit(); },
                     form.mixed([](T &c) { return c.minLength; }));
        addNumberRow(section, "Max Length", 0.f, 10000.f, data->maxLength, [data, emit](float v) { data->maxLength = v; emit(); },
                     form.mixed([](T &c) { return c.maxLength; }));
        addToggleRow(section, "Enable Motor", data->enableMotor, [data, emit](bool v) { data->enableMotor = v; emit(); },
                     form.mixed([](T &c) { return c.enableMotor; }));
        addNumberRow(section, "Motor Speed", -100.f, 100.f, data->motorSpeed, [data, emit](float v) { data->motorSpeed = v; emit(); },
                     form.mixed([](T &c) { return c.motorSpeed; }));
        addNumberRow(section, "Max Force", 0.f, 1000000.f, data->maxMotorForce,
                     [data, emit](float v) { data->maxMotorForce = v; emit(); }, form.mixed([](T &c) { return c.maxMotorForce; }));
    }

    void buildFilterJoint3DSection(CollapsibleSection &section, const ComponentInspectorContext &context) {
        using T = Panda::FilterJoint3DComponent;
        Section<T> form(section, context);
        auto emit = form.emitter(Rebuild::Joint);
        addConnectedRow(form, emit, "Connected");
        section.addContent(makeFormHint("The two bodies never collide with each other."));
    }

    void buildMotorJoint3DSection(CollapsibleSection &section, const ComponentInspectorContext &context) {
        using T = Panda::MotorJoint3DComponent;
        Section<T> form(section, context);
        auto data = form.data;
        auto emit = form.emitter(Rebuild::Joint);
        addConnectedRow(form, emit, "Connected");
        addCollideConnectedRow(form, emit);
        section.addContent(makeFormHint(
            "Drives the velocity of the connected body relative to this one, in world axes; a positive hertz adds a spring "
            "back to the start pose."
        ));
        addVector3Row(section, "Linear Velocity", data->linearVelocity,
                      [data, emit](glm::vec3 v) { data->linearVelocity = v; emit(); },
                      form.vectorMixed([](T &c) { return c.linearVelocity; }));
        addNumberRow(section, "Max Force", 0.f, 1000000.f, data->maxVelocityForce,
                     [data, emit](float v) { data->maxVelocityForce = v; emit(); },
                     form.mixed([](T &c) { return c.maxVelocityForce; }));
        addVector3Row(section, "Angular Velocity", data->angularVelocity,
                      [data, emit](glm::vec3 v) { data->angularVelocity = v; emit(); },
                      form.vectorMixed([](T &c) { return c.angularVelocity; }));
        addNumberRow(section, "Max Torque", 0.f, 1000000.f, data->maxVelocityTorque,
                     [data, emit](float v) { data->maxVelocityTorque = v; emit(); },
                     form.mixed([](T &c) { return c.maxVelocityTorque; }));
        addNumberRow(section, "Linear Hertz", 0.f, 60.f, data->linearHertz, [data, emit](float v) { data->linearHertz = v; emit(); },
                     form.mixed([](T &c) { return c.linearHertz; }));
        addNumberRow(section, "Linear Damping", 0.f, 10.f, data->linearDampingRatio,
                     [data, emit](float v) { data->linearDampingRatio = v; emit(); },
                     form.mixed([](T &c) { return c.linearDampingRatio; }));
        addNumberRow(section, "Max Spring Force", 0.f, 1000000.f, data->maxSpringForce,
                     [data, emit](float v) { data->maxSpringForce = v; emit(); }, form.mixed([](T &c) { return c.maxSpringForce; }));
        addNumberRow(section, "Angular Hertz", 0.f, 60.f, data->angularHertz,
                     [data, emit](float v) { data->angularHertz = v; emit(); }, form.mixed([](T &c) { return c.angularHertz; }));
        addNumberRow(section, "Angular Damping", 0.f, 10.f, data->angularDampingRatio,
                     [data, emit](float v) { data->angularDampingRatio = v; emit(); },
                     form.mixed([](T &c) { return c.angularDampingRatio; }));
        addNumberRow(section, "Max Spring Torque", 0.f, 1000000.f, data->maxSpringTorque,
                     [data, emit](float v) { data->maxSpringTorque = v; emit(); },
                     form.mixed([](T &c) { return c.maxSpringTorque; }));
    }

    template<typename T>
    ComponentInspector inspector(
        const char *component, const char *displayName,
        void (*build)(CollapsibleSection &, const ComponentInspectorContext &)
    ) {
        ComponentInspector entry;
        entry.component = component;
        entry.displayName = displayName;
        entry.setPresence = setPresence<T>;
        entry.build = build;
        return entry;
    }

    // ---- The viewport: collider outlines in EDIT and SIMULATE, by the editor setting.

    void appendColliderOverlay(
        Panda::World &world, std::vector<Panda::HelperLine> &lines, const glm::mat4 &viewProj, PandaUI::Rect viewportFrame
    ) {
        const Panda::PhysicsEditorSettings &settings =
            editorHost().settings(Panda::SettingsScope::Editor).get<Panda::PhysicsEditorSettings>();
        if (settings.showColliders) { Panda::PhysicsEditor::appendColliderLines(world, lines, viewProj, viewportFrame); }
        if (settings.showJoints) { Panda::PhysicsEditor::appendJointLines(world, lines, viewProj, viewportFrame); }
    }

    // ---- The PHYSICS section of the editor settings.

    void buildPhysicsSettings(CollapsibleSection &section, Panda::TypeContext &store) {
        auto data = std::make_shared<Panda::PhysicsEditorSettings>(store.get<Panda::PhysicsEditorSettings>());
        auto emit = [data] { commitSettings<Panda::PhysicsEditorSettings>(Panda::SettingsScope::Editor, *data); };
        addToggleRow(section, "Show Colliders", data->showColliders, [data, emit](bool on) {
            data->showColliders = on;
            emit();
        });
        addToggleRow(section, "Show Joints", data->showJoints, [data, emit](bool on) {
            data->showJoints = on;
            emit();
        });
    }

} // namespace

void registerExtensions(EditorExtensions &extensions) {
    using namespace Panda;
    // The editor-scope section of the module: registered with the settings registry like the
    // sections of the editor itself; the editor adopts it from the loaded settings.json.
    GameContext::getExtensions().settings.add(settingsType<PhysicsEditorSettings>(SettingsScope::Editor, "physics"));

    extensions.componentInspectors.add(inspector<Rigidbody2DComponent>(
        "rigidbody2dComponent", "Rigidbody 2D", buildRigidbodySection<Rigidbody2DComponent>
    ));
    extensions.componentInspectors.add(
        inspector<BoxCollider2DComponent>("boxCollider2dComponent", "Box Collider 2D", buildBoxCollider2DSection)
    );
    extensions.componentInspectors.add(inspector<Rigidbody3DComponent>(
        "rigidbody3dComponent", "Rigidbody 3D", buildRigidbodySection<Rigidbody3DComponent>
    ));
    extensions.componentInspectors.add(
        inspector<BoxCollider3DComponent>("boxCollider3dComponent", "Box Collider 3D", buildBoxCollider3DSection)
    );
    extensions.componentInspectors.add(inspector<SphereCollider3DComponent>(
        "sphereCollider3dComponent", "Sphere Collider 3D", buildSphereCollider3DSection
    ));
    extensions.componentInspectors.add(inspector<CapsuleCollider3DComponent>(
        "capsuleCollider3dComponent", "Capsule Collider 3D", buildCapsuleCollider3DSection
    ));
    extensions.componentInspectors.add(
        inspector<MeshCollider3DComponent>("meshCollider3dComponent", "Mesh Collider 3D", buildMeshCollider3DSection)
    );
    extensions.componentInspectors.add(inspector<RevoluteJoint2DComponent>(
        "revoluteJoint2dComponent", "Revolute Joint 2D", buildRevoluteJoint2DSection
    ));
    extensions.componentInspectors.add(inspector<DistanceJoint2DComponent>(
        "distanceJoint2dComponent", "Distance Joint 2D", buildDistanceJoint2DSection
    ));
    extensions.componentInspectors.add(inspector<RevoluteJoint3DComponent>(
        "revoluteJoint3dComponent", "Revolute Joint 3D", buildRevoluteJoint3DSection
    ));
    extensions.componentInspectors.add(inspector<SphericalJoint3DComponent>(
        "sphericalJoint3dComponent", "Spherical Joint 3D", buildSphericalJoint3DSection
    ));
    extensions.componentInspectors.add(
        inspector<WheelJoint3DComponent>("wheelJoint3dComponent", "Wheel Joint 3D", buildWheelJoint3DSection)
    );
    extensions.componentInspectors.add(
        inspector<WeldJoint3DComponent>("weldJoint3dComponent", "Weld Joint 3D", buildWeldJoint3DSection)
    );
    extensions.componentInspectors.add(inspector<PrismaticJoint3DComponent>(
        "prismaticJoint3dComponent", "Prismatic Joint 3D", buildPrismaticJoint3DSection
    ));
    extensions.componentInspectors.add(inspector<DistanceJoint3DComponent>(
        "distanceJoint3dComponent", "Distance Joint 3D", buildDistanceJoint3DSection
    ));
    extensions.componentInspectors.add(
        inspector<FilterJoint3DComponent>("filterJoint3dComponent", "Filter Joint 3D", buildFilterJoint3DSection)
    );
    extensions.componentInspectors.add(
        inspector<MotorJoint3DComponent>("motorJoint3dComponent", "Motor Joint 3D", buildMotorJoint3DSection)
    );

    extensions.viewportOverlays.push_back({"colliders", appendColliderOverlay});
    extensions.settingsSections.push_back({SettingsScope::Editor, "PHYSICS", buildPhysicsSettings});
}

} // namespace PhysicsEditor
