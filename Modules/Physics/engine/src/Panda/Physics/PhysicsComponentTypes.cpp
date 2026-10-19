#include "Panda/Physics/PhysicsExtensions.hpp"
#include "Panda/Physics/PhysicsComponents.hpp"
#include "Panda/Physics/PhysicsDtos.hpp"

#include <Panda/Extensions/ComponentTypes.hpp>
#include <Panda/GameLogic/Entity.hpp>

namespace Panda::Physics {

namespace {

    // --- 2D physics.

    void encodeRigidbody2D(Rain::Encoder &encoder, Entity entity) {
        Rigidbody2DComponent &body = entity.getComponent<Rigidbody2DComponent>();
        Rigidbody2DComponentDto dto;
        dto.type = body.type;
        dto.fixedRotation = body.fixedRotation;
        encoder.encode("rigidbody2dComponent", dto);
    }
    void decodeRigidbody2D(Rain::Decoder &decoder, Entity entity) {
        Rigidbody2DComponentDto dto;
        if (!decoder.decode("rigidbody2dComponent", dto)) { return; }
        Rigidbody2DComponent &body = entity.addComponent<Rigidbody2DComponent>();
        body.type = dto.type;
        body.fixedRotation = dto.fixedRotation;
    }

    void encodeBoxCollider2D(Rain::Encoder &encoder, Entity entity) {
        BoxCollider2DComponent &collider = entity.getComponent<BoxCollider2DComponent>();
        BoxCollider2DComponentDto dto;
        dto.isSensor = collider.isSensor;
        dto.offset = collider.offset;
        dto.size = collider.size;
        dto.density = collider.density;
        dto.friction = collider.friction;
        dto.restitution = collider.restitution;
        encoder.encode("boxCollider2dComponent", dto);
    }
    void decodeBoxCollider2D(Rain::Decoder &decoder, Entity entity) {
        BoxCollider2DComponentDto dto;
        if (!decoder.decode("boxCollider2dComponent", dto)) { return; }
        BoxCollider2DComponent &collider = entity.addComponent<BoxCollider2DComponent>();
        collider.isSensor = dto.isSensor;
        collider.offset = dto.offset;
        collider.size = dto.size;
        collider.density = dto.density;
        collider.friction = dto.friction;
        collider.restitution = dto.restitution;
    }

    // --- 3D physics.

    void encodeRigidbody3D(Rain::Encoder &encoder, Entity entity) {
        Rigidbody3DComponent &body = entity.getComponent<Rigidbody3DComponent>();
        Rigidbody3DComponentDto dto;
        dto.type = body.type;
        dto.fixedRotation = body.fixedRotation;
        encoder.encode("rigidbody3dComponent", dto);
    }
    void decodeRigidbody3D(Rain::Decoder &decoder, Entity entity) {
        Rigidbody3DComponentDto dto;
        if (!decoder.decode("rigidbody3dComponent", dto)) { return; }
        Rigidbody3DComponent &body = entity.addComponent<Rigidbody3DComponent>();
        body.type = dto.type;
        body.fixedRotation = dto.fixedRotation;
    }

    void encodeBoxCollider3D(Rain::Encoder &encoder, Entity entity) {
        BoxCollider3DComponent &collider = entity.getComponent<BoxCollider3DComponent>();
        BoxCollider3DComponentDto dto;
        dto.isSensor = collider.isSensor;
        dto.offset = collider.offset;
        dto.size = collider.size;
        dto.density = collider.density;
        dto.friction = collider.friction;
        dto.restitution = collider.restitution;
        encoder.encode("boxCollider3dComponent", dto);
    }
    void decodeBoxCollider3D(Rain::Decoder &decoder, Entity entity) {
        BoxCollider3DComponentDto dto;
        if (!decoder.decode("boxCollider3dComponent", dto)) { return; }
        BoxCollider3DComponent &collider = entity.addComponent<BoxCollider3DComponent>();
        collider.isSensor = dto.isSensor;
        collider.offset = dto.offset;
        collider.size = dto.size;
        collider.density = dto.density;
        collider.friction = dto.friction;
        collider.restitution = dto.restitution;
    }

    void encodeSphereCollider3D(Rain::Encoder &encoder, Entity entity) {
        SphereCollider3DComponent &collider = entity.getComponent<SphereCollider3DComponent>();
        SphereCollider3DComponentDto dto;
        dto.isSensor = collider.isSensor;
        dto.offset = collider.offset;
        dto.radius = collider.radius;
        dto.density = collider.density;
        dto.friction = collider.friction;
        dto.restitution = collider.restitution;
        encoder.encode("sphereCollider3dComponent", dto);
    }
    void decodeSphereCollider3D(Rain::Decoder &decoder, Entity entity) {
        SphereCollider3DComponentDto dto;
        if (!decoder.decode("sphereCollider3dComponent", dto)) { return; }
        SphereCollider3DComponent &collider = entity.addComponent<SphereCollider3DComponent>();
        collider.isSensor = dto.isSensor;
        collider.offset = dto.offset;
        collider.radius = dto.radius;
        collider.density = dto.density;
        collider.friction = dto.friction;
        collider.restitution = dto.restitution;
    }

    void encodeCapsuleCollider3D(Rain::Encoder &encoder, Entity entity) {
        CapsuleCollider3DComponent &collider = entity.getComponent<CapsuleCollider3DComponent>();
        CapsuleCollider3DComponentDto dto;
        dto.isSensor = collider.isSensor;
        dto.offset = collider.offset;
        dto.radius = collider.radius;
        dto.halfHeight = collider.halfHeight;
        dto.density = collider.density;
        dto.friction = collider.friction;
        dto.restitution = collider.restitution;
        encoder.encode("capsuleCollider3dComponent", dto);
    }
    void decodeCapsuleCollider3D(Rain::Decoder &decoder, Entity entity) {
        CapsuleCollider3DComponentDto dto;
        if (!decoder.decode("capsuleCollider3dComponent", dto)) { return; }
        CapsuleCollider3DComponent &collider = entity.addComponent<CapsuleCollider3DComponent>();
        collider.isSensor = dto.isSensor;
        collider.offset = dto.offset;
        collider.radius = dto.radius;
        collider.halfHeight = dto.halfHeight;
        collider.density = dto.density;
        collider.friction = dto.friction;
        collider.restitution = dto.restitution;
    }

    // --- Joints.

    void encodeRevoluteJoint2D(Rain::Encoder &encoder, Entity entity) {
        RevoluteJoint2DComponent &joint = entity.getComponent<RevoluteJoint2DComponent>();
        RevoluteJoint2DComponentDto dto;
        dto.connectedEntity = joint.connectedEntity;
        dto.anchor = Vec2(joint.anchor.x, joint.anchor.y);
        dto.enableLimit = joint.enableLimit;
        dto.lowerAngle = joint.lowerAngle;
        dto.upperAngle = joint.upperAngle;
        dto.enableMotor = joint.enableMotor;
        dto.motorSpeed = joint.motorSpeed;
        dto.maxMotorTorque = joint.maxMotorTorque;
        encoder.encode("revoluteJoint2dComponent", dto);
    }
    void decodeRevoluteJoint2D(Rain::Decoder &decoder, Entity entity) {
        RevoluteJoint2DComponentDto dto;
        if (!decoder.decode("revoluteJoint2dComponent", dto)) { return; }
        RevoluteJoint2DComponent &joint = entity.addComponent<RevoluteJoint2DComponent>();
        joint.connectedEntity = dto.connectedEntity;
        joint.anchor = {dto.anchor.x, dto.anchor.y};
        joint.enableLimit = dto.enableLimit;
        joint.lowerAngle = dto.lowerAngle;
        joint.upperAngle = dto.upperAngle;
        joint.enableMotor = dto.enableMotor;
        joint.motorSpeed = dto.motorSpeed;
        joint.maxMotorTorque = dto.maxMotorTorque;
    }

    void encodeDistanceJoint2D(Rain::Encoder &encoder, Entity entity) {
        DistanceJoint2DComponent &joint = entity.getComponent<DistanceJoint2DComponent>();
        DistanceJoint2DComponentDto dto;
        dto.connectedEntity = joint.connectedEntity;
        dto.anchor = Vec2(joint.anchor.x, joint.anchor.y);
        dto.connectedAnchor = Vec2(joint.connectedAnchor.x, joint.connectedAnchor.y);
        dto.length = joint.length;
        dto.enableSpring = joint.enableSpring;
        dto.hertz = joint.hertz;
        dto.dampingRatio = joint.dampingRatio;
        encoder.encode("distanceJoint2dComponent", dto);
    }
    void decodeDistanceJoint2D(Rain::Decoder &decoder, Entity entity) {
        DistanceJoint2DComponentDto dto;
        if (!decoder.decode("distanceJoint2dComponent", dto)) { return; }
        DistanceJoint2DComponent &joint = entity.addComponent<DistanceJoint2DComponent>();
        joint.connectedEntity = dto.connectedEntity;
        joint.anchor = {dto.anchor.x, dto.anchor.y};
        joint.connectedAnchor = {dto.connectedAnchor.x, dto.connectedAnchor.y};
        joint.length = dto.length;
        joint.enableSpring = dto.enableSpring;
        joint.hertz = dto.hertz;
        joint.dampingRatio = dto.dampingRatio;
    }

    void encodeRevoluteJoint3D(Rain::Encoder &encoder, Entity entity) {
        RevoluteJoint3DComponent &joint = entity.getComponent<RevoluteJoint3DComponent>();
        RevoluteJoint3DComponentDto dto;
        dto.connectedEntity = joint.connectedEntity;
        dto.anchor = Vec3(joint.anchor.x, joint.anchor.y, joint.anchor.z);
        dto.axis = Vec3(joint.axis.x, joint.axis.y, joint.axis.z);
        dto.enableLimit = joint.enableLimit;
        dto.lowerAngle = joint.lowerAngle;
        dto.upperAngle = joint.upperAngle;
        dto.enableMotor = joint.enableMotor;
        dto.motorSpeed = joint.motorSpeed;
        dto.maxMotorTorque = joint.maxMotorTorque;
        encoder.encode("revoluteJoint3dComponent", dto);
    }
    void decodeRevoluteJoint3D(Rain::Decoder &decoder, Entity entity) {
        RevoluteJoint3DComponentDto dto;
        if (!decoder.decode("revoluteJoint3dComponent", dto)) { return; }
        RevoluteJoint3DComponent &joint = entity.addComponent<RevoluteJoint3DComponent>();
        joint.connectedEntity = dto.connectedEntity;
        joint.anchor = {dto.anchor.x, dto.anchor.y, dto.anchor.z};
        joint.axis = {dto.axis.x, dto.axis.y, dto.axis.z};
        joint.enableLimit = dto.enableLimit;
        joint.lowerAngle = dto.lowerAngle;
        joint.upperAngle = dto.upperAngle;
        joint.enableMotor = dto.enableMotor;
        joint.motorSpeed = dto.motorSpeed;
        joint.maxMotorTorque = dto.maxMotorTorque;
    }

    void encodeSphericalJoint3D(Rain::Encoder &encoder, Entity entity) {
        SphericalJoint3DComponent &joint = entity.getComponent<SphericalJoint3DComponent>();
        SphericalJoint3DComponentDto dto;
        dto.connectedEntity = joint.connectedEntity;
        dto.anchor = Vec3(joint.anchor.x, joint.anchor.y, joint.anchor.z);
        dto.axis = Vec3(joint.axis.x, joint.axis.y, joint.axis.z);
        dto.enableConeLimit = joint.enableConeLimit;
        dto.coneAngle = joint.coneAngle;
        dto.enableTwistLimit = joint.enableTwistLimit;
        dto.lowerTwistAngle = joint.lowerTwistAngle;
        dto.upperTwistAngle = joint.upperTwistAngle;
        encoder.encode("sphericalJoint3dComponent", dto);
    }
    void decodeSphericalJoint3D(Rain::Decoder &decoder, Entity entity) {
        SphericalJoint3DComponentDto dto;
        if (!decoder.decode("sphericalJoint3dComponent", dto)) { return; }
        SphericalJoint3DComponent &joint = entity.addComponent<SphericalJoint3DComponent>();
        joint.connectedEntity = dto.connectedEntity;
        joint.anchor = {dto.anchor.x, dto.anchor.y, dto.anchor.z};
        joint.axis = {dto.axis.x, dto.axis.y, dto.axis.z};
        joint.enableConeLimit = dto.enableConeLimit;
        joint.coneAngle = dto.coneAngle;
        joint.enableTwistLimit = dto.enableTwistLimit;
        joint.lowerTwistAngle = dto.lowerTwistAngle;
        joint.upperTwistAngle = dto.upperTwistAngle;
    }

    void encodeWheelJoint3D(Rain::Encoder &encoder, Entity entity) {
        WheelJoint3DComponent &joint = entity.getComponent<WheelJoint3DComponent>();
        WheelJoint3DComponentDto dto;
        dto.connectedEntity = joint.connectedEntity;
        dto.anchor = Vec3(joint.anchor.x, joint.anchor.y, joint.anchor.z);
        dto.axis = Vec3(joint.axis.x, joint.axis.y, joint.axis.z);
        dto.suspensionAxis = Vec3(joint.suspensionAxis.x, joint.suspensionAxis.y, joint.suspensionAxis.z);
        dto.enableSuspensionSpring = joint.enableSuspensionSpring;
        dto.suspensionHertz = joint.suspensionHertz;
        dto.suspensionDampingRatio = joint.suspensionDampingRatio;
        dto.enableSuspensionLimit = joint.enableSuspensionLimit;
        dto.lowerSuspensionLimit = joint.lowerSuspensionLimit;
        dto.upperSuspensionLimit = joint.upperSuspensionLimit;
        dto.enableSteering = joint.enableSteering;
        dto.steeringHertz = joint.steeringHertz;
        dto.steeringDampingRatio = joint.steeringDampingRatio;
        dto.maxSteeringTorque = joint.maxSteeringTorque;
        dto.enableSteeringLimit = joint.enableSteeringLimit;
        dto.lowerSteeringAngle = joint.lowerSteeringAngle;
        dto.upperSteeringAngle = joint.upperSteeringAngle;
        dto.enableMotor = joint.enableMotor;
        dto.motorSpeed = joint.motorSpeed;
        dto.maxMotorTorque = joint.maxMotorTorque;
        encoder.encode("wheelJoint3dComponent", dto);
    }
    void decodeWheelJoint3D(Rain::Decoder &decoder, Entity entity) {
        WheelJoint3DComponentDto dto;
        if (!decoder.decode("wheelJoint3dComponent", dto)) { return; }
        WheelJoint3DComponent &joint = entity.addComponent<WheelJoint3DComponent>();
        joint.connectedEntity = dto.connectedEntity;
        joint.anchor = {dto.anchor.x, dto.anchor.y, dto.anchor.z};
        joint.axis = {dto.axis.x, dto.axis.y, dto.axis.z};
        joint.suspensionAxis = {dto.suspensionAxis.x, dto.suspensionAxis.y, dto.suspensionAxis.z};
        joint.enableSuspensionSpring = dto.enableSuspensionSpring;
        joint.suspensionHertz = dto.suspensionHertz;
        joint.suspensionDampingRatio = dto.suspensionDampingRatio;
        joint.enableSuspensionLimit = dto.enableSuspensionLimit;
        joint.lowerSuspensionLimit = dto.lowerSuspensionLimit;
        joint.upperSuspensionLimit = dto.upperSuspensionLimit;
        joint.enableSteering = dto.enableSteering;
        joint.steeringHertz = dto.steeringHertz;
        joint.steeringDampingRatio = dto.steeringDampingRatio;
        joint.maxSteeringTorque = dto.maxSteeringTorque;
        joint.enableSteeringLimit = dto.enableSteeringLimit;
        joint.lowerSteeringAngle = dto.lowerSteeringAngle;
        joint.upperSteeringAngle = dto.upperSteeringAngle;
        joint.enableMotor = dto.enableMotor;
        joint.motorSpeed = dto.motorSpeed;
        joint.maxMotorTorque = dto.maxMotorTorque;
    }

} // namespace

void registerComponentTypes(ComponentTypeRegistry &registry) {
    registry.add(componentType<Rigidbody2DComponent>("rigidbody2dComponent", encodeRigidbody2D, decodeRigidbody2D));
    registry.add(componentType<BoxCollider2DComponent>("boxCollider2dComponent", encodeBoxCollider2D, decodeBoxCollider2D));
    registry.add(componentType<Rigidbody3DComponent>("rigidbody3dComponent", encodeRigidbody3D, decodeRigidbody3D));
    registry.add(componentType<BoxCollider3DComponent>("boxCollider3dComponent", encodeBoxCollider3D, decodeBoxCollider3D));
    registry.add(componentType<SphereCollider3DComponent>(
        "sphereCollider3dComponent", encodeSphereCollider3D, decodeSphereCollider3D
    ));
    registry.add(componentType<CapsuleCollider3DComponent>(
        "capsuleCollider3dComponent", encodeCapsuleCollider3D, decodeCapsuleCollider3D
    ));
    registry.add(componentType<RevoluteJoint2DComponent>(
        "revoluteJoint2dComponent", encodeRevoluteJoint2D, decodeRevoluteJoint2D
    ));
    registry.add(componentType<DistanceJoint2DComponent>(
        "distanceJoint2dComponent", encodeDistanceJoint2D, decodeDistanceJoint2D
    ));
    registry.add(componentType<RevoluteJoint3DComponent>(
        "revoluteJoint3dComponent", encodeRevoluteJoint3D, decodeRevoluteJoint3D
    ));
    registry.add(componentType<SphericalJoint3DComponent>(
        "sphericalJoint3dComponent", encodeSphericalJoint3D, decodeSphericalJoint3D
    ));
    registry.add(componentType<WheelJoint3DComponent>("wheelJoint3dComponent", encodeWheelJoint3D, decodeWheelJoint3D));
}

} // namespace Panda::Physics
