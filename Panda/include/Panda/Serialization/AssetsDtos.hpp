#pragma once

#include "Panda/Base/Base.hpp"
#include "Panda/Assets/Base/AssetRef.hpp"
#include "Panda/GameLogic/GameContext.hpp"

#include <Foundation/Foundation.hpp>
#include <Rain/Rain.hpp>
#include <variant>

namespace Panda {

// TODO: Add Vec2, Vec3, Mat3, Mat4
// COLOR — a vec4 color in sRGB (as the artist sees it in the color picker); on upload to the
// GPU it is converted to linear light. VEC4 — raw data, uploaded as is.
// The values are serialized as numbers in .mat — add new types only before UNKNOWN.
enum class MaterialFieldType : uint32_t { FLOAT, UINT, VEC4, TEXTURE, COLOR, UNKNOWN };

using MaterialFieldValue = std::variant<uint32_t, float, Vec4, AssetRef<Asset>>;

struct MaterialFieldDto : public Rain::Codable {
    std::string name;
    MaterialFieldType type;
    MaterialFieldValue value;

    static void encode(const char *key, Rain::Encoder *encoder, const MaterialFieldDto &data) {
        encoder->beginObject(key);
        encoder->encode("name", data.name);
        encoder->encode("type", data.type);
        switch (data.type) {
            case MaterialFieldType::FLOAT: {
                float value = std::get<float>(data.value);
                encoder->encode("value", value);
                break;
            }
            case MaterialFieldType::UINT: {
                uint32_t value = std::get<uint32_t>(data.value);
                encoder->encode("value", value);
                break;
            }
            case MaterialFieldType::VEC4:
            case MaterialFieldType::COLOR: {
                // COLOR is stored on disk in authored sRGB — converted only on upload to the GPU.
                Vec4 value = std::get<Vec4>(data.value);
                encoder->encode("value", value);
                break;
            }
            case MaterialFieldType::TEXTURE: {
                AssetRef<Asset> value = std::get<AssetRef<Asset>>(data.value);
                encoder->encode("value", value.getId());
                break;
            }
            default: {
                PND_ASSERT(false, "Unknown field type");
                break;
            }
        }
        encoder->endObject();
    }

    static bool decode(const char *key, Rain::Decoder *decoder, MaterialFieldDto &data) {
        if (!decoder->beginObject(key)) { return false; }
        decoder->decode("name", data.name);
        decoder->decode("type", data.type);
        switch (data.type) {
            case MaterialFieldType::FLOAT: {
                float value = 0;
                decoder->decode("value", value);
                data.value = value;
                break;
            }
            case MaterialFieldType::UINT: {
                uint32_t value = 0;
                decoder->decode("value", value);
                data.value = value;
                break;
            }
            case MaterialFieldType::VEC4:
            case MaterialFieldType::COLOR: {
                Vec4 value;
                decoder->decode("value", value);
                data.value = value;
                break;
            }
            case MaterialFieldType::TEXTURE: {
                UUID value;
                decoder->decode("value", value);
                AssetRef<Asset> texture = AssetRef<Asset>(value);
                data.value = texture;
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

struct MaterialDataDto : public Rain::Codable {
    std::vector<MaterialFieldDto> inputs;

    MaterialDataDto()
        : inputs() {}

    RAIN_FIELDS_BEGIN(MaterialDataDto)
    RAIN_FIELD(inputs)
    RAIN_FIELDS_END
};

// The .postfx file: an ordered list of the passes of a postprocessing chain.
struct PostprocessingPassDto : public Rain::Codable {
    UUID material = 0;
    bool enabled = true;
    uint32_t downsample = 1; // resolution divisor of the pass target (the default for old .postfx)

    RAIN_FIELDS_BEGIN(PostprocessingPassDto)
    RAIN_FIELD(material)
    RAIN_FIELD(enabled)
    RAIN_FIELD(downsample)
    RAIN_FIELDS_END
};

struct PostprocessingDataDto : public Rain::Codable {
    std::vector<PostprocessingPassDto> passes;
    bool handlesTonemap = false; // chain carries its own tonemap — the built-in one is not added

    RAIN_FIELDS_BEGIN(PostprocessingDataDto)
    RAIN_FIELD(passes)
    RAIN_FIELD(handlesTonemap)
    RAIN_FIELDS_END
};

} // namespace Panda