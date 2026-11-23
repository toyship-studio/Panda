#pragma once

#include "Panda/Assets/Base/AssetFileReference.hpp"
#include "Panda/Assets/Base/Path.hpp"
#include "Panda/Assets/Base/Asset.hpp"
#include "Panda/Serialization/AssetsDtos.hpp"

#include <Rain/Rain.hpp>
#include <Miren/Miren.hpp>
#include <cstdint>
#include <variant>

namespace Panda {

// Asset passport — the ONLY description: id + type + settings (nested types below).
// There are no paths here: where the asset lives is known by the registry (id → AssetFileReference,
// filled by the scan from the sidecar's location). The settings are what a file cannot hold
// itself: the import of a picture, the materials of the slots of a model. A file of the engine's
// own format holds what it is in itself (the shader of a material is in its .mat), so a material,
// a shader, a world, a postprocessing chain and the contributed kinds have none — monostate. The
// same form is serialized into the sidecar X.<ext>.pmeta (the settings JSON key is the historical
// "meta") and, paired with AssetFileReference, into the export's assets.bin.
struct AssetInfo : public Rain::Codable {
    struct TextureSettings : public Rain::Codable {
        Miren::TextureFiltering minFiltering = Miren::NEAREST;
        Miren::TextureFiltering magFiltering = Miren::NEAREST;
        uint32_t numMips = 0;
        // Color texture in sRGB: created in an sRGB format, sampling decodes to linear.
        // Data textures (normals, metallic/roughness, occlusion, masks) — false.
        bool srgb = true;
        // Block compression (BC7) in the cooked file and in video memory: a quarter of RGBA8,
        // with a small loss. Off for what must be exact — pixel art, a lookup table, a mask
        // read by its exact values.
        bool compressed = true;

        RAIN_FIELDS_BEGIN(TextureSettings)
        RAIN_FIELD(minFiltering)
        RAIN_FIELD(magFiltering)
        RAIN_FIELD(numMips)
        RAIN_FIELD(srgb)
        RAIN_FIELD(compressed)
        RAIN_FIELDS_END
    };

    struct GltfMaterial : public Rain::Codable {
        std::string name;
        path_t baseColorTexturePath;
        Vec4 baseColorFactor = Vec4(1.f, 1.f, 1.f, 1.f);
        path_t metallicRoughnessTexturePath;
        float metallicFactor = 1.f;
        float roughnessFactor = 1.f;
        path_t normalTexturePath;
        float normalScale = 1.f;
        path_t occlusionTexturePath;
        float occlusionStrength = 1.f;
        path_t emissiveTexturePath;
        Vec4 emissiveFactor = Vec4(0.f, 0.f, 0.f, 1.f);
        uint32_t alphaMode = 0;
        float alphaCutoff = 0.5f;
        bool doubleSided = false;
        float emissiveStrength = 1.f;   // KHR_materials_emissive_strength
        float transmissionFactor = 0.f; // KHR_materials_transmission: glass
        // Material of the SLOT (slot = import material, primitives share it): a direct id —
        // any project material can go into the slot, the runtime reads it without resolving
        // by paths. 0 — the slot is unbound (the renderer paints with a fallback). Mesh
        // document, written on Save.
        UUID materialAssetId = 0;

        RAIN_FIELDS_BEGIN(GltfMaterial)
        RAIN_FIELD(name)
        RAIN_FIELD(baseColorTexturePath)
        RAIN_FIELD(baseColorFactor)
        RAIN_FIELD(metallicRoughnessTexturePath)
        RAIN_FIELD(metallicFactor)
        RAIN_FIELD(roughnessFactor)
        RAIN_FIELD(normalTexturePath)
        RAIN_FIELD(normalScale)
        RAIN_FIELD(occlusionTexturePath)
        RAIN_FIELD(occlusionStrength)
        RAIN_FIELD(emissiveTexturePath)
        RAIN_FIELD(emissiveFactor)
        RAIN_FIELD(alphaMode)
        RAIN_FIELD(alphaCutoff)
        RAIN_FIELD(doubleSided)
        RAIN_FIELD(emissiveStrength)
        RAIN_FIELD(transmissionFactor)
        RAIN_FIELD(materialAssetId)
        RAIN_FIELDS_END
    };

    struct GltfPrimitiveMaterial : public Rain::Codable {
        uint32_t submeshIndex = 0;
        uint32_t primitiveIndex = 0;
        int32_t materialIndex = -1;

        RAIN_FIELDS_BEGIN(GltfPrimitiveMaterial)
        RAIN_FIELD(submeshIndex)
        RAIN_FIELD(primitiveIndex)
        RAIN_FIELD(materialIndex)
        RAIN_FIELDS_END
    };

    struct MeshSettings : public Rain::Codable {
        std::vector<GltfMaterial> materials;
        std::vector<GltfPrimitiveMaterial> primitiveMaterials;

        RAIN_FIELDS_BEGIN(MeshSettings)
        RAIN_FIELD(materials)
        RAIN_FIELD(primitiveMaterials)
        RAIN_FIELDS_END
    };

    using Settings = std::variant<std::monostate, TextureSettings, MeshSettings>;

    AssetId id;
    AssetType type = AssetType::NONE;
    Settings settings;

    // Only a texture and an imported model have settings.
    static void encode(const char *key, Rain::Encoder *encoder, const AssetInfo &data) {
        encoder->beginObject(key);
        encoder->encode("id", data.id);
        encoder->encode("type", data.type.value);
        if (data.type == AssetType::TEXTURE) {
            encoder->encode("meta", std::get<TextureSettings>(data.settings));
        } else if (data.type == AssetType::IMPORTED_MESH) {
            encoder->encode("meta", std::get<MeshSettings>(data.settings));
        }
        encoder->endObject();
    }

    static bool decode(const char *key, Rain::Decoder *decoder, AssetInfo &data) {
        if (!decoder->beginObject(key)) { return false; }
        decoder->decode("id", data.id);
        decoder->decode("type", data.type.value);
        data.settings = std::monostate{};
        if (data.type == AssetType::TEXTURE) {
            TextureSettings settings;
            decoder->decode("meta", settings);
            data.settings = settings;
        } else if (data.type == AssetType::IMPORTED_MESH) {
            MeshSettings settings;
            decoder->decode("meta", settings);
            data.settings = settings;
        }
        decoder->endObject();
        return true;
    }

    // Kind name for messages — from the kind registry (Extensions::assetKinds).
    const char *getTypeStr() const;
};

// Runtime registry entry (assets.bin): passport + source. In the editor the source lives in
// a separate map (the source of truth is the file location); export bakes the pair, converting
// the path into the runtime layout.
struct AssetRegistryEntryDto : public Rain::Codable {
    AssetInfo info;
    AssetFileReference source;

    RAIN_FIELDS_BEGIN(AssetRegistryEntryDto)
    RAIN_FIELD(info)
    RAIN_FIELD(source)
    RAIN_FIELDS_END
};

struct AssetRegistryDto : public Rain::Codable {
    std::vector<AssetRegistryEntryDto> assets;

    RAIN_FIELDS_BEGIN(AssetRegistryDto)
    RAIN_FIELD(assets)
    RAIN_FIELDS_END
};

} // namespace Panda
