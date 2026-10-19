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
    // reaches the simulation: the bodies of the targets are rebuilt from the new values (a
    // rebuilt body keeps its pose and velocities and loses its joints; a joint component is
    // read at simulation start only).

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

        std::function<void()> emitter(bool bodies) const {
            Panda::World *world = &this->world;
            const std::vector<Panda::Entity> entities = this->entities;
            std::shared_ptr<T> data = this->data;
            return [world, entities, data, bodies] {
                commitComponent<T>(*world, entities, *data);
                if (!bodies || !world->isRunning()) { return; }
                Panda::Physics2D *physics2D = world->findContext<Panda::Physics2D>();
                Panda::Physics3D *physics3D = world->findContext<Panda::Physics3D>();
                for (Panda::Entity entity : entities) {
                    if (!entity.hasComponent<T>()) { continue; }
                    if (physics2D) { physics2D->propertiesUpdated(entity); }
                    if (physics3D) { physics3D->propertiesUpdated(entity); }
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
        auto emit = form.emitter(true);
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
        auto emit = form.emitter(true);
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
        auto emit = form.emitter(true);
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
        auto emit = form.emitter(true);
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
        auto emit = form.emitter(true);
        addSensorRow(form, emit);
        addVector3Row(section, "Offset", data->offset, [data, emit](glm::vec3 v) { data->offset = v; emit(); },
                      form.vectorMixed([](T &c) { return c.offset; }));
        addNumberRow(section, "Radius", 0.001f, 1000.f, data->radius, [data, emit](float v) { data->radius = v; emit(); },
                     form.mixed([](T &c) { return c.radius; }));
        addNumberRow(section, "Half Height", 0.f, 1000.f, data->halfHeight, [data, emit](float v) { data->halfHeight = v; emit(); },
                     form.mixed([](T &c) { return c.halfHeight; }));
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
        auto emit = form.emitter(false);
        addConnectedRow(form, emit, "Connected");
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
        auto emit = form.emitter(false);
        addConnectedRow(form, emit, "Connected");
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
        auto emit = form.emitter(false);
        addConnectedRow(form, emit, "Connected");
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
        auto emit = form.emitter(false);
        addConnectedRow(form, emit, "Connected");
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
        auto emit = form.emitter(false);
        // The joint sits on the wheel: the connected entity is the chassis.
        addConnectedRow(form, emit, "Chassis");
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
        if (!editorHost().settings(Panda::SettingsScope::Editor).get<Panda::PhysicsEditorSettings>().showColliders) {
            return;
        }
        Panda::PhysicsEditor::appendColliderLines(world, lines, viewProj, viewportFrame);
    }

    // ---- The PHYSICS section of the editor settings.

    void buildPhysicsSettings(CollapsibleSection &section, Panda::TypeContext &store) {
        auto data = std::make_shared<Panda::PhysicsEditorSettings>(store.get<Panda::PhysicsEditorSettings>());
        auto emit = [data] { commitSettings<Panda::PhysicsEditorSettings>(Panda::SettingsScope::Editor, *data); };
        addToggleRow(section, "Show Colliders", data->showColliders, [data, emit](bool on) {
            data->showColliders = on;
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

    extensions.viewportOverlays.push_back({"colliders", appendColliderOverlay});
    extensions.settingsSections.push_back({SettingsScope::Editor, "PHYSICS", buildPhysicsSettings});
}

} // namespace PhysicsEditor
