#pragma once

#include "Panda/Physics/PhysicsComponents.hpp"

#include <Panda/Base/Base.hpp>
#include <Rain/Rain.hpp>

namespace Panda {

// The .world shape of the physics components (C++ field name = JSON key).

struct Rigidbody2DComponentDto : public Rain::Codable {
    Rigidbody2DComponent::BodyType type;
    bool fixedRotation;

    static void encode(const char *key, Rain::Encoder *encoder, const Rigidbody2DComponentDto &data) {
        encoder->beginObject(key);
        encoder->encode("type", data.type);
        encoder->encode("fixedRotation", data.fixedRotation);
        encoder->endObject();
    }

    static bool decode(const char *key, Rain::Decoder *decoder, Rigidbody2DComponentDto &data) {
        if (!decoder->beginObject(key)) { return false; }
        decoder->decode("type", data.type);
        decoder->decode("fixedRotation", data.fixedRotation);
        decoder->endObject();
        return true;
    }
};

struct BoxCollider2DComponentDto : public Rain::Codable {
    bool isSensor;
    Vec2 offset;
    Vec2 size;
    float density;
    float friction;
    float restitution;

    RAIN_FIELDS_BEGIN(BoxCollider2DComponentDto)
    RAIN_FIELD(isSensor)
    RAIN_FIELD(offset)
    RAIN_FIELD(size)
    RAIN_FIELD(density)
    RAIN_FIELD(friction)
    RAIN_FIELD(restitution)
    RAIN_FIELDS_END
};

struct Rigidbody3DComponentDto : public Rain::Codable {
    Rigidbody3DComponent::BodyType type;
    bool fixedRotation = false; // default: old .world files without the key decode to false

    static void encode(const char *key, Rain::Encoder *encoder, const Rigidbody3DComponentDto &data) {
        encoder->beginObject(key);
        encoder->encode("type", data.type);
        encoder->encode("fixedRotation", data.fixedRotation);
        encoder->endObject();
    }

    static bool decode(const char *key, Rain::Decoder *decoder, Rigidbody3DComponentDto &data) {
        if (!decoder->beginObject(key)) { return false; }
        decoder->decode("type", data.type);
        decoder->decode("fixedRotation", data.fixedRotation);
        decoder->endObject();
        return true;
    }
};

struct BoxCollider3DComponentDto : public Rain::Codable {
    bool isSensor;
    Vec3 offset;
    Vec3 size;
    float density;
    float friction;
    float restitution;

    RAIN_FIELDS_BEGIN(BoxCollider3DComponentDto)
    RAIN_FIELD(isSensor)
    RAIN_FIELD(offset)
    RAIN_FIELD(size)
    RAIN_FIELD(density)
    RAIN_FIELD(friction)
    RAIN_FIELD(restitution)
    RAIN_FIELDS_END
};

struct SphereCollider3DComponentDto : public Rain::Codable {
    bool isSensor;
    Vec3 offset;
    float radius;
    float density;
    float friction;
    float restitution;

    RAIN_FIELDS_BEGIN(SphereCollider3DComponentDto)
    RAIN_FIELD(isSensor)
    RAIN_FIELD(offset)
    RAIN_FIELD(radius)
    RAIN_FIELD(density)
    RAIN_FIELD(friction)
    RAIN_FIELD(restitution)
    RAIN_FIELDS_END
};

struct CapsuleCollider3DComponentDto : public Rain::Codable {
    bool isSensor;
    Vec3 offset;
    float radius;
    float halfHeight;
    float density;
    float friction;
    float restitution;

    RAIN_FIELDS_BEGIN(CapsuleCollider3DComponentDto)
    RAIN_FIELD(isSensor)
    RAIN_FIELD(offset)
    RAIN_FIELD(radius)
    RAIN_FIELD(halfHeight)
    RAIN_FIELD(density)
    RAIN_FIELD(friction)
    RAIN_FIELD(restitution)
    RAIN_FIELDS_END
};

struct RevoluteJoint2DComponentDto : public Rain::Codable {
    UUID connectedEntity = 0;
    Vec2 anchor;
    bool enableLimit = false;
    float lowerAngle = -45.f;
    float upperAngle = 45.f;
    bool enableMotor = false;
    float motorSpeed = 90.f;
    float maxMotorTorque = 10.f;

    RAIN_FIELDS_BEGIN(RevoluteJoint2DComponentDto)
    RAIN_FIELD(connectedEntity)
    RAIN_FIELD(anchor)
    RAIN_FIELD(enableLimit)
    RAIN_FIELD(lowerAngle)
    RAIN_FIELD(upperAngle)
    RAIN_FIELD(enableMotor)
    RAIN_FIELD(motorSpeed)
    RAIN_FIELD(maxMotorTorque)
    RAIN_FIELDS_END
};

struct DistanceJoint2DComponentDto : public Rain::Codable {
    UUID connectedEntity = 0;
    Vec2 anchor;
    Vec2 connectedAnchor;
    float length = 0.f;
    bool enableSpring = false;
    float hertz = 4.f;
    float dampingRatio = 0.5f;

    RAIN_FIELDS_BEGIN(DistanceJoint2DComponentDto)
    RAIN_FIELD(connectedEntity)
    RAIN_FIELD(anchor)
    RAIN_FIELD(connectedAnchor)
    RAIN_FIELD(length)
    RAIN_FIELD(enableSpring)
    RAIN_FIELD(hertz)
    RAIN_FIELD(dampingRatio)
    RAIN_FIELDS_END
};

struct RevoluteJoint3DComponentDto : public Rain::Codable {
    UUID connectedEntity = 0;
    Vec3 anchor;
    Vec3 axis;
    bool enableLimit = false;
    float lowerAngle = -45.f;
    float upperAngle = 45.f;
    bool enableMotor = false;
    float motorSpeed = 90.f;
    float maxMotorTorque = 10.f;

    RAIN_FIELDS_BEGIN(RevoluteJoint3DComponentDto)
    RAIN_FIELD(connectedEntity)
    RAIN_FIELD(anchor)
    RAIN_FIELD(axis)
    RAIN_FIELD(enableLimit)
    RAIN_FIELD(lowerAngle)
    RAIN_FIELD(upperAngle)
    RAIN_FIELD(enableMotor)
    RAIN_FIELD(motorSpeed)
    RAIN_FIELD(maxMotorTorque)
    RAIN_FIELDS_END
};

struct SphericalJoint3DComponentDto : public Rain::Codable {
    UUID connectedEntity = 0;
    Vec3 anchor;
    Vec3 axis;
    bool enableConeLimit = false;
    float coneAngle = 30.f;
    bool enableTwistLimit = false;
    float lowerTwistAngle = -30.f;
    float upperTwistAngle = 30.f;

    RAIN_FIELDS_BEGIN(SphericalJoint3DComponentDto)
    RAIN_FIELD(connectedEntity)
    RAIN_FIELD(anchor)
    RAIN_FIELD(axis)
    RAIN_FIELD(enableConeLimit)
    RAIN_FIELD(coneAngle)
    RAIN_FIELD(enableTwistLimit)
    RAIN_FIELD(lowerTwistAngle)
    RAIN_FIELD(upperTwistAngle)
    RAIN_FIELDS_END
};

struct WheelJoint3DComponentDto : public Rain::Codable {
    UUID connectedEntity = 0;
    Vec3 anchor;
    Vec3 axis = Vec3(1.f, 0.f, 0.f);
    Vec3 suspensionAxis = Vec3(0.f, 1.f, 0.f);
    bool enableSuspensionSpring = true;
    float suspensionHertz = 5.f;
    float suspensionDampingRatio = 0.7f;
    bool enableSuspensionLimit = true;
    float lowerSuspensionLimit = -0.1f;
    float upperSuspensionLimit = 0.1f;
    bool enableSteering = false;
    float steeringHertz = 10.f;
    float steeringDampingRatio = 1.f;
    float maxSteeringTorque = 1000.f;
    bool enableSteeringLimit = true;
    float lowerSteeringAngle = -35.f;
    float upperSteeringAngle = 35.f;
    bool enableMotor = false;
    float motorSpeed = 0.f;
    float maxMotorTorque = 100.f;

    RAIN_FIELDS_BEGIN(WheelJoint3DComponentDto)
    RAIN_FIELD(connectedEntity)
    RAIN_FIELD(anchor)
    RAIN_FIELD(axis)
    RAIN_FIELD(suspensionAxis)
    RAIN_FIELD(enableSuspensionSpring)
    RAIN_FIELD(suspensionHertz)
    RAIN_FIELD(suspensionDampingRatio)
    RAIN_FIELD(enableSuspensionLimit)
    RAIN_FIELD(lowerSuspensionLimit)
    RAIN_FIELD(upperSuspensionLimit)
    RAIN_FIELD(enableSteering)
    RAIN_FIELD(steeringHertz)
    RAIN_FIELD(steeringDampingRatio)
    RAIN_FIELD(maxSteeringTorque)
    RAIN_FIELD(enableSteeringLimit)
    RAIN_FIELD(lowerSteeringAngle)
    RAIN_FIELD(upperSteeringAngle)
    RAIN_FIELD(enableMotor)
    RAIN_FIELD(motorSpeed)
    RAIN_FIELD(maxMotorTorque)
    RAIN_FIELDS_END
};

} // namespace Panda
