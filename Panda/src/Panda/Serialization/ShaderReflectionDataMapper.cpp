#include "Panda/Serialization/ShaderReflectionDataMapper.hpp"

#include <algorithm>

namespace Panda {

MaterialFieldType toMaterialFieldType(std::string type) {
    if (type == "float") {
        return MaterialFieldType::FLOAT;
    } else if (type == "vec4") {
        return MaterialFieldType::VEC4;
    } else if (type == "uint") {
        return MaterialFieldType::UINT;
    } else {
        return MaterialFieldType::UNKNOWN;
    }
}

MaterialFieldValue getDefaultValueFor(MaterialFieldType type) {
    switch (type) {
        case MaterialFieldType::FLOAT:
            return float(0.f);
        case MaterialFieldType::UINT:
            return uint32_t(0);
        case MaterialFieldType::VEC4:
            return Vec4(0.f, 0.f, 0.f, 0.f);
        case MaterialFieldType::COLOR:
            // Opaque white — a neutral multiplier for the color factors.
            return Vec4(1.f, 1.f, 1.f, 1.f);
        case MaterialFieldType::TEXTURE:
            return AssetRef<Asset>();
        case MaterialFieldType::UNKNOWN:
            break;
    }
    return float(0.f);
}

MaterialData ShaderReflectionDataMapper::toMaterialData(const ShaderSpirvReflectionData &reflection) {
    MaterialData data;
    ShaderUniformBufferInfo ubo;
    for (auto item : reflection.ubos) {
        if (item.name == "type.MATERIAL_FIELDS") {
            ubo = item;
            break;
        }
    }
    ShaderTypeInfo typeInfo;
    if (reflection.types.contains(ubo.type)) { typeInfo = reflection.types.at(ubo.type); }
    for (auto &member : typeInfo.members) {
        MaterialFieldType type = toMaterialFieldType(member.type);
        // The `// @color` annotation in HLSL promotes a vec4 to a color (sRGB → linear on load).
        if (type == MaterialFieldType::VEC4) {
            const auto &colors = reflection.color_fields;
            if (std::find(colors.begin(), colors.end(), member.name) != colors.end()) {
                type = MaterialFieldType::COLOR;
            }
        }
        data.inputs.emplace_back(MaterialField(member.name, type, getDefaultValueFor(type)));
    }
    for (auto image : reflection.separate_images) {
        // The system light textures (the SCENE_LIGHTS contract) are fed by the renderer every
        // frame — they do not belong to the material author.
        if (image.name == "occlusionVolume" || image.name == "shadowMap" || image.name == "environmentMap") {
            continue;
        }
        MaterialFieldType type = MaterialFieldType::TEXTURE;
        data.inputs.emplace_back(MaterialField(image.name, type, getDefaultValueFor(type)));
    }
    return data;
}

} // namespace Panda