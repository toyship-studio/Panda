#include "Assets/AssetHandlerRuntime.hpp"
#include "Panda/Assets/Base/AssetImporterBase.hpp"
#include "Panda/Assets/Base/CookedMesh.hpp"
#include "Panda/Runtime/GameFiles.hpp"
#include "Panda/Assets/Base/TextureCodec.hpp"
#include "Panda/Assets/Fallback/FallbackAssets.hpp"
#include "Panda/Assets/TextureAsset.hpp"
#include "Panda/Assets/ShaderAsset.hpp"
#include "Panda/Assets/MaterialAsset.hpp"
#include "Panda/Assets/ImportedMeshAsset.hpp"
#include "Panda/Assets/PostprocessingAsset.hpp"
#include "Panda/Assets/WorldAsset.hpp"
#include "Panda/Prefabs/PrefabAsset.hpp"
#include "Panda/Serialization/WorldMapper.hpp"
#include "Panda/Extensions/Extensions.hpp"
#include "Panda/GameLogic/GameContext.hpp"
#include "Panda/Serialization/AssetsMapper.hpp"

#include <cstdint>
#include <cstring>
#include <fstream>

namespace {

Panda::path_t stripPandaPrefix(const Panda::path_t &path) {
    Panda::path_t result;
    auto it = path.begin();
    if (it != path.end() && *it == Panda::path_t(".Panda")) { ++it; }
    for (; it != path.end(); ++it) {
        result /= *it;
    }
    return result;
}

} // namespace

namespace Panda {

AssetHandlerRuntime::AssetHandlerRuntime()
    : AssetHandler(Foundation::getAllocator())
    , m_runtimeDataPath()
    , m_binaryDecoder() {}

void AssetHandlerRuntime::setRuntimeDataPath(const path_t &runtimeDataPath) {
    m_runtimeDataPath = runtimeDataPath;
}

bool AssetHandlerRuntime::loadRegistry() {
    path_t registryPath = m_runtimeDataPath / "assets.bin";
    const std::unique_ptr<std::istream> file = GameFiles::open(registryPath);
    if (!file) {
        LOG_ERROR("RUNTIME: Asset registry not found at %s", registryPath.string().c_str());
        return false;
    }
    AssetRegistryDto registryDto;
    Rain::Decoder *decoder = &m_binaryDecoder;
    decoder->decode(*file, registryDto);
    for (auto &entry : registryDto.assets) {
        m_registry[entry.info.id] = entry.info;
        m_assetSources[entry.info.id] = entry.source;
    }
    LOG_INFO("RUNTIME: Loaded %d assets from registry", (int)m_registry.size());
    return true;
}

void AssetHandlerRuntime::close() {
    m_registry.clear();
    m_assetSources.clear();
    m_runtimeDataPath.clear();
}

const AssetInfo *AssetHandlerRuntime::getAssetInfo(AssetId id) const {
    auto it = m_registry.find(id);
    if (it == m_registry.end()) { return nullptr; }
    return &it->second;
}

bool AssetHandlerRuntime::hasAsset(AssetId id) const {
    return m_loadedAssets.contains(id) || m_registry.contains(id);
}

bool AssetHandlerRuntime::readMeshData(AssetId id, std::vector<ImportedMeshData> &meshes) {
    meshes.clear();
    const AssetInfo *assetInfo = getAssetInfo(id);
    if (!assetInfo || assetInfo->type != AssetType::IMPORTED_MESH) { return false; }
    const std::unique_ptr<std::istream> meshFile = GameFiles::open(m_runtimeDataPath / assetPath(id));
    return meshFile && CookedMesh::read(*meshFile, meshes) && !meshes.empty();
}

bool AssetHandlerRuntime::readTexturePixelsRGBA8(
    AssetId id, uint32_t *width, uint32_t *height, std::vector<unsigned char> &pixels
) {
    if (width) { *width = 0; }
    if (height) { *height = 0; }
    pixels.clear();

    const AssetInfo *assetInfo = getAssetInfo(id);
    if (!assetInfo || assetInfo->type != AssetType::TEXTURE) { return false; }

    const path_t cookedPath = m_runtimeDataPath / assetPath(id);
    if (!GameFiles::exists(cookedPath)) {
        LOG_ERROR("RUNTIME: Texture %u not found at %s", (uint32_t)id, cookedPath.string().c_str());
        return false;
    }

    Miren::TextureCreate create = AssetImporterBase::loadCookedTexture(cookedPath);
    const bool copied = TextureCodec::readPixelsRGBA8(create, width, height, pixels);
    create.m_data.release();
    if (!copied) { LOG_ERROR("RUNTIME: Texture %u has no RGBA8 pixels to read", (uint32_t)id); }
    return copied;
}

Asset *AssetHandlerRuntime::loadInternal(AssetId id, bool forcedReload) {
    if (!forcedReload && m_loadedAssets.contains(id)) { return m_loadedAssets.at(id).asset; }
    if (!m_registry.contains(id)) {
        LOG_ERROR("RUNTIME: Unknown asset ID %u", (uint32_t)id);
        return nullptr;
    }
    auto assetInfo = m_registry.at(id);
    const path_t entryPath = assetPath(id);
    Asset *asset = nullptr;
    const AssetType loadedType = assetInfo.type;
    if (loadedType == AssetType::TEXTURE) {
        auto meta = std::get<AssetInfo::TextureSettings>(assetInfo.settings);
        path_t cookedPath = m_runtimeDataPath / entryPath;
        if (GameFiles::exists(cookedPath)) {
            Miren::TextureCreate create = AssetImporterBase::loadCookedTexture(cookedPath);
            TextureCodec::applySettings(create, meta);
            asset = F_NEW(m_allocator, TextureAsset)(create);
            LOG_INFO("RUNTIME: Loaded texture %u from %s", (uint32_t)id, entryPath.string().c_str());
        } else {
            asset = F_NEW(m_allocator, FallbackTextureAsset)();
            LOG_ERROR("RUNTIME: Texture %u not found at %s", (uint32_t)id, entryPath.string().c_str());
        }
    } else if (loadedType == AssetType::SHADER) {
        // The pair's path is the vert source; cooked file names are deterministic from the sources.
        const Foundation::GraphicsAPI backend = Foundation::ACTIVE_GRAPHICS_API;
        path_t vertCookedPath = AssetImporterBase::getCookedShaderAssetPath(entryPath, backend);
        path_t fragCookedPath =
            AssetImporterBase::getCookedShaderAssetPath(replaceLastChars(entryPath, "frag.hlsl"), backend);
        path_t fullVertPath = m_runtimeDataPath / stripPandaPrefix(vertCookedPath);
        path_t fullFragPath = m_runtimeDataPath / stripPandaPrefix(fragCookedPath);
        if (GameFiles::exists(fullVertPath) && GameFiles::exists(fullFragPath)) {
            asset = F_NEW(m_allocator, ShaderAsset)(fullVertPath, fullFragPath);
            LOG_INFO("RUNTIME: Loaded shader %u", (uint32_t)id);
        } else {
            asset = F_NEW(m_allocator, ShaderAsset)();
            LOG_ERROR("RUNTIME: Shader %u cooked files not found", (uint32_t)id);
        }
    } else if (loadedType == AssetType::PREFAB) {
        // The entities of the prefab as the cook wrote them — plain ones, in binary — in the
        // world of the asset: what World::instantiate copies.
        PrefabAsset *prefab = F_NEW(m_allocator, PrefabAsset)();
        asset = prefab;
        const path_t prefabPath = m_runtimeDataPath / entryPath;
        const std::unique_ptr<std::istream> file = GameFiles::open(prefabPath);
        if (file && WorldMapper::load(prefab->world(), m_binaryDecoder, *file)) {
            LOG_INFO("RUNTIME: Loaded prefab %u from %s", (uint32_t)id, entryPath.string().c_str());
        } else {
            LOG_ERROR("RUNTIME: Prefab %u not found at %s", (uint32_t)id, entryPath.string().c_str());
        }
    } else if (loadedType == AssetType::MATERIAL) {
        auto meta = std::get<AssetInfo::MaterialSettings>(assetInfo.settings);
        path_t materialPath = m_runtimeDataPath / entryPath;
        MaterialDataDto dataDto;
        if (const std::unique_ptr<std::istream> binFile = GameFiles::open(materialPath)) {
            Rain::Decoder *decoder = &m_binaryDecoder;
            decoder->decode(*binFile, dataDto);
            // Type guard: a legacy meta.shader may point to a non-shader — Lit fallback.
            AssetRef<ShaderAsset> shader;
            if (meta.shader) {
                AssetRef<Asset> shaderAsset{meta.shader};
                if (shaderAsset && shaderAsset->getType() == AssetType::SHADER) {
                    shader = shaderAsset.as<ShaderAsset>();
                } else {
                    LOG_ERROR(
                        "RUNTIME: Material %u shader %u missing, falling back", (uint32_t)id, (uint32_t)meta.shader
                    );
                }
            }
            MaterialData data;
            AssetsMapper::toData(data, dataDto);
            asset = F_NEW(m_allocator, MaterialAsset)(data, shader);
            LOG_INFO("RUNTIME: Loaded material %u from %s", (uint32_t)id, materialPath.string().c_str());
        } else {
            asset = F_NEW(m_allocator, FallbackMaterialAsset)();
            LOG_ERROR("RUNTIME: Material %u not found at %s", (uint32_t)id, materialPath.string().c_str());
        }
    } else if (loadedType == AssetType::IMPORTED_MESH) {
        auto meta = std::get<AssetInfo::MeshSettings>(assetInfo.settings);
        path_t meshPath = m_runtimeDataPath / entryPath;
        // The geometry was read from the model at the cook (CookedMesh): the game loads arrays.
        std::vector<ImportedMeshData> meshes;
        const std::unique_ptr<std::istream> meshFile = GameFiles::open(meshPath);
        if (meshFile && CookedMesh::read(*meshFile, meshes)) {
            ImportedMeshAsset *mesh = F_NEW(m_allocator, ImportedMeshAsset)(meshes);
            // The material layout — from the meta (direct ids): the same source as in the editor.
            std::vector<AssetRef<Asset>> slots;
            slots.reserve(meta.materials.size());
            for (const AssetInfo::GltfMaterial &material : meta.materials) {
                slots.emplace_back(AssetId(material.materialAssetId));
            }
            std::vector<int32_t> primitiveSlots;
            primitiveSlots.reserve(meta.primitiveMaterials.size());
            for (const AssetInfo::GltfPrimitiveMaterial &primitive : meta.primitiveMaterials) {
                primitiveSlots.push_back(primitive.materialIndex);
            }
            mesh->setupMaterials(std::move(slots), std::move(primitiveSlots));
            asset = mesh;
            LOG_INFO("RUNTIME: Loaded mesh %u from %s", (uint32_t)id, entryPath.string().c_str());
        } else {
            asset = F_NEW(m_allocator, FallbackImportedMeshAsset)();
            LOG_ERROR("RUNTIME: Mesh %u not found at %s", (uint32_t)id, entryPath.string().c_str());
        }
    } else if (loadedType == AssetType::WORLD) {
        asset = F_NEW(m_allocator, WorldAsset)();
    } else if (loadedType == AssetType::POSTPROCESSING) {
        path_t postfxPath = m_runtimeDataPath / entryPath;
        PostprocessingDataDto dataDto;
        if (const std::unique_ptr<std::istream> binFile = GameFiles::open(postfxPath)) {
            Rain::Decoder *decoder = &m_binaryDecoder;
            decoder->decode(*binFile, dataDto);
            std::vector<PostprocessingPass> passes;
            for (PostprocessingPassDto &passDto : dataDto.passes) {
                PostprocessingPass pass;
                if (passDto.material) { pass.material = AssetRef<Asset>(passDto.material); }
                pass.enabled = passDto.enabled;
                pass.downsample = passDto.downsample;
                passes.push_back(pass);
            }
            asset = F_NEW(m_allocator, PostprocessingAsset)(std::move(passes));
            LOG_INFO("RUNTIME: Loaded postprocessing %u from %s", (uint32_t)id, postfxPath.string().c_str());
        } else {
            asset = F_NEW(m_allocator, PostprocessingAsset)();
            LOG_ERROR("RUNTIME: Postprocessing %u not found at %s", (uint32_t)id, postfxPath.string().c_str());
        }
    } else {
        const AssetKind *kind = GameContext::getExtensions().assetKinds.find(assetInfo.type);
        if (kind != nullptr && kind->fileBased()) {
            // A file-based kind from the registry (plugins): the same format as in the editor (the
            // export is a copy).
            const path_t assetPath = m_runtimeDataPath / entryPath;
            asset = kind->load(m_allocator, assetPath);
            if (asset != nullptr) {
                LOG_INFO(
                    "RUNTIME: Loaded %s %u from %s", kind->displayName.c_str(), (uint32_t)id, assetPath.string().c_str()
                );
            } else {
                asset = kind->placeholder(m_allocator);
                LOG_ERROR(
                    "RUNTIME: %s %u not found at %s",
                    kind->displayName.c_str(),
                    (uint32_t)id,
                    assetPath.string().c_str()
                );
            }
        } else {
            asset = nullptr;
        }
    }
    if (!asset) {
        LOG_ERROR("RUNTIME: Failed to load asset %u of type %u", (uint32_t)id, assetInfo.type.value);
        return nullptr;
    }
    if (forcedReload && m_loadedAssets.contains(id)) {
        Asset *prevAsset = m_loadedAssets.at(id).asset;
        F_DELETE(m_allocator, prevAsset); // the destructor releases what the asset owns
    }
    AssetEntry entry;
    entry.asset = asset;
    m_loadedAssets[id] = entry;
    return asset;
}

} // namespace Panda
