#pragma once

#include "Panda/GameLogic/Components/BaseComponents.hpp"

#include <Rain/Rain.hpp>

#include <cstdint>

namespace Panda {

struct TransformComponentDto : public Rain::Codable {
    Vec3 position;
    Quat rotation;
    Vec3 scale;

    RAIN_FIELDS_BEGIN(TransformComponentDto)
    RAIN_FIELD(position)
    RAIN_FIELD(rotation)
    RAIN_FIELD(scale)
    RAIN_FIELDS_END
};

struct WorldCameraDto : public Rain::Codable {
    WorldCamera::ProjectionType projectionType;
    float fieldOfView;
    float orthoSize;
    float zNear;
    float zFar;

    static void encode(const char *key, Rain::Encoder *encoder, const WorldCameraDto &data) {
        encoder->beginObject(key);
        int projectionType = (int)data.projectionType;
        encoder->encode("projectionType", projectionType);
        encoder->encode("fieldOfView", data.fieldOfView);
        encoder->encode("orthoSize", data.orthoSize);
        encoder->encode("zNear", data.zNear);
        encoder->encode("zFar", data.zFar);
        encoder->endObject();
    }

    static bool decode(const char *key, Rain::Decoder *decoder, WorldCameraDto &data) {
        if (!decoder->beginObject(key)) { return false; }
        int projectionType;
        decoder->decode("projectionType", projectionType);
        data.projectionType = (WorldCamera::ProjectionType)projectionType;
        decoder->decode("fieldOfView", data.fieldOfView);
        decoder->decode("orthoSize", data.orthoSize);
        decoder->decode("zNear", data.zNear);
        decoder->decode("zFar", data.zFar);
        decoder->endObject();
        return true;
    }
};

struct SpriteRendererComponentDto : public Rain::Codable {
    Color color;
    int cols = 1;
    int rows = 1;
    int index = 0;
    UUID material = UUID(0);
    UUID texture = UUID(0);

    SpriteRendererComponentDto() = default;
    SpriteRendererComponentDto(const SpriteRendererComponentDto &other) = default;

    RAIN_FIELDS_BEGIN(SpriteRendererComponentDto)
    RAIN_FIELD(color)
    RAIN_FIELD(cols)
    RAIN_FIELD(rows)
    RAIN_FIELD(index)
    RAIN_FIELD(material)
    RAIN_FIELD(texture)
    RAIN_FIELDS_END
};

struct ImportedSubmeshComponentDto : public Rain::Codable {
    UUID importedMesh;
    // The field name was CHANGED together with the semantics (it used to be "material" — a copy
    // of the import default in every entity): the override is now empty by default, the
    // primitive follows the slot layout of the mesh asset. The old key in .world is ignored and
    // disappears on re-save.
    UUID overrideMaterial = 0;
    uint32_t submeshIndex;
    uint32_t primitiveIndex;

    ImportedSubmeshComponentDto() = default;
    ImportedSubmeshComponentDto(const ImportedSubmeshComponentDto &other) = default;

    RAIN_FIELDS_BEGIN(ImportedSubmeshComponentDto)
    RAIN_FIELD(importedMesh)
    RAIN_FIELD(overrideMaterial)
    RAIN_FIELD(submeshIndex)
    RAIN_FIELD(primitiveIndex)
    RAIN_FIELDS_END
};

struct CameraComponentDto : public Rain::Codable {
    bool isPrimary;
    WorldCameraDto camera;
    UUID postprocessing = 0; // PostprocessingAsset id (0 — none; the default for old .world)
    bool postprocessingEnabled = true;
    bool bloomEnabled = false;
    float bloomIntensity = 1.f;
    float bloomRadius = 1.f;
    bool autoExposureEnabled = false;
    float autoExposureKey = 0.18f;
    float autoExposureSpeed = 2.f;
    float autoExposureMin = 0.25f;
    float autoExposureMax = 4.f;

    RAIN_FIELDS_BEGIN(CameraComponentDto)
    RAIN_FIELD(isPrimary)
    RAIN_FIELD(camera)
    RAIN_FIELD(postprocessing)
    RAIN_FIELD(postprocessingEnabled)
    RAIN_FIELD(bloomEnabled)
    RAIN_FIELD(bloomIntensity)
    RAIN_FIELD(bloomRadius)
    RAIN_FIELD(autoExposureEnabled)
    RAIN_FIELD(autoExposureKey)
    RAIN_FIELD(autoExposureSpeed)
    RAIN_FIELD(autoExposureMin)
    RAIN_FIELD(autoExposureMax)
    RAIN_FIELDS_END
};

struct DirectionalLightComponentDto : public Rain::Codable {
    Color color = Color(1.f, 1.f, 1.f, 1.f);
    float intensity = 1.f;
    bool isEnabled = true;
    bool castShadows = true;

    RAIN_FIELDS_BEGIN(DirectionalLightComponentDto)
    RAIN_FIELD(color)
    RAIN_FIELD(intensity)
    RAIN_FIELD(isEnabled)
    RAIN_FIELD(castShadows)
    RAIN_FIELDS_END
};

struct PointLightComponentDto : public Rain::Codable {
    Color color = Color(1.f, 1.f, 1.f, 1.f);
    float intensity = 1.f;
    float radius = 10.f;
    bool isEnabled = true;

    RAIN_FIELDS_BEGIN(PointLightComponentDto)
    RAIN_FIELD(color)
    RAIN_FIELD(intensity)
    RAIN_FIELD(radius)
    RAIN_FIELD(isEnabled)
    RAIN_FIELDS_END
};

struct SkyComponentDto : public Rain::Codable {
    UUID material;

    RAIN_FIELDS_BEGIN(SkyComponentDto)
    RAIN_FIELD(material)
    RAIN_FIELDS_END
};

// Greyboxing primitive: kind — stable semantics (not the internal asset id).
// RuntimeMeshComponent is deliberately NOT serialized: its meshes are created by code at
// runtime (scripts), their content does not exist on disk.
struct PrimitiveMeshComponentDto : public Rain::Codable {
    uint32_t kind = 0; // PrimitiveMeshKind
    UUID material;

    RAIN_FIELDS_BEGIN(PrimitiveMeshComponentDto)
    RAIN_FIELD(kind)
    RAIN_FIELD(material)
    RAIN_FIELDS_END
};

struct ScriptFieldDto : public Rain::Codable {
    std::string name;
    uint32_t fieldId;
    ScriptFieldType type;
    ScriptFieldValue value;

    static void encode(const char *key, Rain::Encoder *encoder, const ScriptFieldDto &data) {
        encoder->beginObject(key);
        encoder->encode("name", data.name);
        encoder->encode("fieldId", data.fieldId);
        encoder->encode("type", data.type);
        switch (data.type) {
            case ScriptFieldType::INTEGER: {
                int value = std::get<int32_t>(data.value);
                encoder->encode("value", value);
                break;
            }
            case ScriptFieldType::FLOAT: {
                float value = std::get<float>(data.value);
                encoder->encode("value", value);
                break;
            }
            case ScriptFieldType::ENTITY:
            case ScriptFieldType::TEXTURE:
            case ScriptFieldType::MATERIAL:
            case ScriptFieldType::WORLD: {
                UUID value = std::get<UUID>(data.value);
                encoder->encode("value", value);
                break;
            }
            default: {
                PND_ASSERT(false, "Unknown field type");
                break;
            }
        }
        encoder->endObject();
    }

    static bool decode(const char *key, Rain::Decoder *decoder, ScriptFieldDto &data) {
        if (!decoder->beginObject(key)) { return false; }
        decoder->decode("name", data.name);
        decoder->decode("fieldId", data.fieldId);
        decoder->decode("type", data.type);
        switch (data.type) {
            case ScriptFieldType::INTEGER: {
                int value = 0;
                decoder->decode("value", value);
                data.value = value;
                break;
            }
            case ScriptFieldType::FLOAT: {
                float value = 0;
                decoder->decode("value", value);
                data.value = value;
                break;
            }
            case ScriptFieldType::MATERIAL:
            case ScriptFieldType::TEXTURE:
            case ScriptFieldType::ENTITY:
            case ScriptFieldType::WORLD: {
                UUID value = 0;
                decoder->decode("value", value);
                data.value = value;
                break;
            }
            default: {
                PND_ASSERT(false, "Unknown field type");
                break;
            }
        }
        decoder->endObject();
        return true;
    }
};

struct ExternalScriptDto : public Rain::Codable {
    std::string name;
    std::vector<ScriptFieldDto> scriptFields;

    ExternalScriptDto()
        : name()
        , scriptFields() {}

    ExternalScriptDto(const std::string &name, const std::vector<ScriptFieldDto> &fields)
        : name(name)
        , scriptFields(fields) {}

    RAIN_FIELDS_BEGIN(ExternalScriptDto)
    RAIN_FIELD(name)
    RAIN_FIELD(scriptFields)
    RAIN_FIELDS_END
};

struct ScriptListComponentDto : public Rain::Codable {
    std::vector<ExternalScriptDto> scripts;

    RAIN_FIELDS_BEGIN(ScriptListComponentDto)
    RAIN_FIELD(scripts)
    RAIN_FIELDS_END
};

} // namespace Panda
