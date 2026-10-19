#include "Panda/Application/Application.hpp"
#include "Panda/Assets/MaterialAsset.hpp"
#include "Panda/Assets/MaterialSystemValues.hpp"
#include "Panda/Base/ColorSpace.hpp"
#include "Panda/Renderer/SceneWind.hpp"
#include "Panda/Renderer/ShaderInterface/ShaderBlocks.hpp"
#include "Panda/Renderer/Std140Buffer.hpp"
#include "Panda/Serialization/ShaderReflectionDataMapper.hpp"
#include "Panda/Assets/StaticResources.hpp"

namespace Panda {

namespace {

    Miren::TextureHandle getFallbackTextureHandle(const std::string &fieldName) {
        if (fieldName == "normalTexture") { return StaticResources::flatNormalTexture->getMirenHandle(); }
        // emissive without a texture = white: the glow is set by emissiveFactor alone (a color)
        // and is turned off by a zero one.
        return StaticResources::whiteTexture->getMirenHandle();
    }

} // namespace

AssetRef<ShaderAsset> MaterialAsset::getShaderAsset() {
    if (m_shaderRef) { return m_shaderRef; }
    return StaticResources::defaultLitShader;
}

void MaterialAsset::bindFields() {
    // Textures are bound on every draw (the draw call state is reset by the submit).
    for (auto &field : m_data.inputs) {
        if (field.type != MaterialFieldType::TEXTURE) { continue; }
        AssetRef<Asset> asset = std::get<AssetRef<Asset>>(field.value);
        Miren::TextureHandle handle = getFallbackTextureHandle(field.name);
        if (asset) {
            AssetRef<TextureAsset> texture = asset.as<TextureAsset>();
            if (texture) { handle = texture->getMirenHandle(); }
        }
        Miren::addInputTexture(field.name, handle);
    }
    // Scalar fields come from the cache; it is rebuilt after a change of a field (setFieldValue)
    // or of a system value (the engine sets the fields of the same name — the registry version is
    // shared).
    const uint32_t systemVersion = MaterialSystemValues::version();
    if (m_systemValuesVersion != systemVersion) {
        m_systemValuesVersion = systemVersion;
        m_fieldsUboDirty = true;
    }
    if (m_fieldsUboDirty) {
        m_fieldsUboDirty = false;
        m_cachedFieldsUbo.clear();
        Std140Buffer ubo;
        buildFieldsUbo(ubo);
        if (ubo.getSize()) {
            const uint8_t *data = static_cast<const uint8_t *>(ubo.getData());
            m_cachedFieldsUbo.assign(data, data + ubo.getSize());
        }
    }
    if (!m_cachedFieldsUbo.empty()) {
        Miren::addInputUniformBuffer(
            "MATERIAL_FIELDS", m_cachedFieldsUbo.data(), static_cast<uint32_t>(m_cachedFieldsUbo.size())
        );
    }
    // Standard frame values (PandaFieldsUbo): time and scene wind.
    PandaFieldsUbo fields{};
    fields.iTime = Panda::Application::get()->getTime();
    fields.sceneWind = SceneWind::phaseVector();
    Miren::addInputUniformBuffer("PANDA_FIELDS", &fields, sizeof(fields));
}

void MaterialAsset::bindTextureField(const char *name) {
    for (auto &field : m_data.inputs) {
        if (field.type != MaterialFieldType::TEXTURE || field.name != name) { continue; }
        AssetRef<Asset> asset = std::get<AssetRef<Asset>>(field.value);
        Miren::TextureHandle handle = getFallbackTextureHandle(field.name);
        if (asset) {
            AssetRef<TextureAsset> texture = asset.as<TextureAsset>();
            if (texture) { handle = texture->getMirenHandle(); }
        }
        Miren::addInputTexture(field.name, handle);
        return;
    }
}

bool MaterialAsset::hasField(const char *name) const {
    for (const auto &field : m_data.inputs) {
        if (field.name == name) { return true; }
    }
    return false;
}

void MaterialAsset::buildFieldsUbo(Std140Buffer &ubo) const {
    for (auto &field : m_data.inputs) {
        // A system value of the same name overrides the material — the shared scene settings
        // apply to all materials at once (the same resolve is used when a material is viewed).
        const MaterialFieldValue &value = MaterialSystemValues::resolve(field);
        switch (field.type) {
            case MaterialFieldType::FLOAT: {
                ubo.addFloat(std::get<float>(value));
                break;
            }
            case MaterialFieldType::UINT: {
                ubo.addUInt(std::get<uint32_t>(value));
                break;
            }
            case MaterialFieldType::VEC4: {
                Vec4 vec = std::get<Vec4>(value);
                ubo.addVec4(vec.x, vec.y, vec.z, vec.w);
                break;
            }
            case MaterialFieldType::COLOR: {
                // The author specifies the color in sRGB, the shaders work in linear light.
                Vec4 vec = srgbToLinear(std::get<Vec4>(value));
                ubo.addVec4(vec.x, vec.y, vec.z, vec.w);
                break;
            }
            case MaterialFieldType::TEXTURE:
                break; // textures are bound by bindFields directly
            default: {
                PND_ASSERT(false, "Unknown field type");
                break;
            }
        }
    }
}

void MaterialAsset::setFieldValue(const char *name, MaterialFieldValue value) {
    for (auto &field : m_data.inputs) {
        if (field.name == name) {
            field.value = value;
            m_fieldsUboDirty = true;
            return;
        }
    }
    LOG_ERROR_EDITOR("MATERIAL FIELD %s NOT FOUND", name);
}

void MaterialAsset::setRuntimeField(const char *name, MaterialFieldValue value) {
    // Silent: a user replacement material is allowed not to declare the slot (the contribution
    // is the default one). The field type is not touched — the value must match the type (a
    // texture into TEXTURE, a vector into VEC4); the runtime flag takes the field out of
    // serialization and out of the inspector.
    for (auto &field : m_data.inputs) {
        if (field.name != name) { continue; }
        field.value = std::move(value);
        field.runtime = true;
        // Textures are not part of the UBO — binding them does not require rebuilding the buffer.
        if (field.type != MaterialFieldType::TEXTURE) { m_fieldsUboDirty = true; }
        return;
    }
}

uint32_t MaterialAsset::getUIntFieldValue(const char *name, uint32_t defaultValue) const {
    for (const auto &field : m_data.inputs) {
        if (field.name != name || field.type != MaterialFieldType::UINT) { continue; }
        return std::get<uint32_t>(field.value);
    }
    return defaultValue;
}

float MaterialAsset::getFloatFieldValue(const char *name, float defaultValue) const {
    for (const auto &field : m_data.inputs) {
        if (field.name != name || field.type != MaterialFieldType::FLOAT) { continue; }
        return std::get<float>(field.value);
    }
    return defaultValue;
}

Vec4 MaterialAsset::getVec4FieldValue(const char *name, Vec4 defaultValue) const {
    for (const auto &field : m_data.inputs) {
        if (field.name != name) { continue; }
        if (field.type != MaterialFieldType::VEC4 && field.type != MaterialFieldType::COLOR) { continue; }
        return std::get<Vec4>(field.value);
    }
    return defaultValue;
}

#ifdef PND_EDITOR

MaterialData &MaterialAsset::getInputs() {
    return m_data;
}

void MaterialAsset::updateFields() {
    if (!m_shaderRef) { return; }
    ShaderSpirvReflectionData reflection = m_shaderRef->getReflectionData();
    MaterialData shaderData = ShaderReflectionDataMapper::toMaterialData(reflection);
    // An empty reflection = there is no data (built-in/binary shader), not "a shader without
    // fields": do not erase the existing material fields.
    if (shaderData.inputs.empty()) { return; }
    for (auto &field : m_data.inputs) {
        if (!shaderData.hasField(field.name)) {
            LOG_INFO_EDITOR("MATERIAL FIELD %s NOT FOUND.", field.name.c_str());
            m_data.removeField(field);
            continue;
        }
    }
    for (const auto &shaderField : shaderData.inputs) {
        // Check if field is new
        if (!m_data.hasField(shaderField.name)) {
            MaterialField field = MaterialField(shaderField.name, shaderField.type, shaderField.value);
            m_data.addField(field);
            continue;
        }
        MaterialField &field = m_data.getField(shaderField.name);
        if (field.type != shaderField.type) {
            // VEC4 ↔ COLOR — the same value, only the interpretation changes:
            // annotating with @color in the shader must not reset the configured color.
            const bool sameShape =
                (field.type == MaterialFieldType::VEC4 || field.type == MaterialFieldType::COLOR) &&
                (shaderField.type == MaterialFieldType::VEC4 || shaderField.type == MaterialFieldType::COLOR);
            field.type = shaderField.type;
            if (!sameShape) { field.value = shaderField.value; }
        }
    }
}

void MaterialAsset::setChanged(bool value) {
    m_isChanged = value;
}

bool MaterialAsset::isChanged() const {
    return m_isChanged;
}

#endif

} // namespace Panda
