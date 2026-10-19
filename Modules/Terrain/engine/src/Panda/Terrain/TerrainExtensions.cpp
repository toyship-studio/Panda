#include "Panda/Terrain/TerrainExtensions.hpp"
#include "Panda/Terrain/TerrainLodSettings.hpp"
#include "Panda/Terrain/TerrainComponents.hpp"
#include "Panda/Assets/Base/AssetRefImpl.hpp"
#include "Panda/Extensions/Extensions.hpp"
#include "Panda/GameLogic/Entity.hpp"
#include "Panda/GameLogic/World.hpp"
#include "Panda/Terrain/TerrainDtos.hpp"
#include "Panda/Terrain/FloraDistances.hpp"
#include "Panda/Terrain/Terrain2DAsset.hpp"
#include "Panda/Terrain/Terrain3DAsset.hpp"
#include "Panda/Terrain/TerrainResources.hpp"
#include "Panda/Terrain/TerrainScriptHost.hpp"
#include "Panda/Terrain/TerrainSerialization.hpp"
#include "Panda/Terrain/TerrainSystem.hpp"

#include <fstream>

namespace Panda {

template class AssetRef<Terrain2DAsset>;
template class AssetRef<Terrain3DAsset>;

namespace Terrain {

    namespace {

        // --- Components.

        void encodeTerrain2D(Rain::Encoder &encoder, Entity entity) {
            Terrain2DComponent &terrain = entity.getComponent<Terrain2DComponent>();
            Terrain2DComponentDto dto;
            dto.terrain = terrain.terrain.getId();
            dto.material = terrain.material.getId();
            dto.physicsEnabled = terrain.physicsEnabled;
            encoder.encode("terrain2dComponent", dto);
        }
        void decodeTerrain2D(Rain::Decoder &decoder, Entity entity) {
            Terrain2DComponentDto dto;
            if (!decoder.decode("terrain2dComponent", dto)) { return; }
            Terrain2DComponent &terrain = entity.addComponent<Terrain2DComponent>();
            terrain.terrain = AssetRef<Asset>(dto.terrain);
            terrain.material = AssetRef<Asset>(dto.material);
            terrain.physicsEnabled = dto.physicsEnabled;
        }

        void encodeTerrain3D(Rain::Encoder &encoder, Entity entity) {
            Terrain3DComponent &terrain = entity.getComponent<Terrain3DComponent>();
            Terrain3DComponentDto dto;
            dto.terrain = terrain.terrain.getId();
            dto.material = terrain.material.getId();
            dto.physicsEnabled = terrain.physicsEnabled;
            encoder.encode("terrain3dComponent", dto);
        }
        void decodeTerrain3D(Rain::Decoder &decoder, Entity entity) {
            Terrain3DComponentDto dto;
            if (!decoder.decode("terrain3dComponent", dto)) { return; }
            Terrain3DComponent &terrain = entity.addComponent<Terrain3DComponent>();
            terrain.terrain = AssetRef<Asset>(dto.terrain);
            terrain.material = AssetRef<Asset>(dto.material);
            terrain.physicsEnabled = dto.physicsEnabled;
        }

        // --- .terrain assets: the kind comes from the file header (2D and 3D share the extension).

        bool headerKindIs(const path_t &path, TerrainKind expected) {
            std::ifstream file(path, std::ios::binary);
            TerrainKind kind;
            return file.is_open() && TerrainSerialization::readHeader(file, kind) && kind == expected;
        }
        bool detect2D(const path_t &path) {
            return headerKindIs(path, TerrainKind::Heightmap2D);
        }
        bool detect3D(const path_t &path) {
            return headerKindIs(path, TerrainKind::Voxels3D);
        }

        Asset *load2D(Foundation::AllocatorI *allocator, const path_t &path) {
            std::ifstream file(path, std::ios::binary);
            TerrainKind kind;
            if (!file.is_open() || !TerrainSerialization::readHeader(file, kind) || kind != TerrainKind::Heightmap2D) {
                return nullptr;
            }
            TerrainData2D data;
            FloraSettings flora;
            TerrainTrees trees;
            if (!TerrainSerialization::read2DPayload(file, data, flora, trees)) { return nullptr; }
            return F_NEW(allocator, Terrain2DAsset)(std::move(data), std::move(flora), std::move(trees));
        }
        Asset *placeholder2D(Foundation::AllocatorI *allocator) {
            return F_NEW(allocator, Terrain2DAsset)(TerrainData2D{});
        }
        bool save2D(Asset &asset, const path_t &path) {
            auto &terrain = static_cast<Terrain2DAsset &>(asset);
            std::ofstream file(path, std::ios::binary);
            if (!file.is_open()) { return false; }
            TerrainSerialization::write2D(file, terrain.getData(), terrain.getFloraSettings(), terrain.getTrees());
            return true;
        }

        Asset *load3D(Foundation::AllocatorI *allocator, const path_t &path) {
            std::ifstream file(path, std::ios::binary);
            TerrainKind kind;
            if (!file.is_open() || !TerrainSerialization::readHeader(file, kind) || kind != TerrainKind::Voxels3D) {
                return nullptr;
            }
            TerrainData3D data;
            if (!TerrainSerialization::read3DPayload(file, data)) { return nullptr; }
            return F_NEW(allocator, Terrain3DAsset)(std::move(data));
        }
        Asset *placeholder3D(Foundation::AllocatorI *allocator) {
            return F_NEW(allocator, Terrain3DAsset)(TerrainData3D{});
        }
        bool save3D(Asset &asset, const path_t &path) {
            auto &terrain = static_cast<Terrain3DAsset &>(asset);
            std::ofstream file(path, std::ios::binary);
            if (!file.is_open()) { return false; }
            TerrainSerialization::write3D(file, terrain.getData());
            return true;
        }

        AssetKind terrainKind(
            const char *name, AssetType type, const char *displayName, bool (*detect)(const path_t &),
            Asset *(*load)(Foundation::AllocatorI *, const path_t &), Asset *(*placeholder)(Foundation::AllocatorI *),
            bool (*save)(Asset &, const path_t &)
        ) {
            AssetKind kind;
            kind.name = name;
            kind.type = type;
            kind.displayName = displayName;
            kind.folder = "Terrains";
            kind.extensions = {".terrain"};
            kind.detect = detect;
            kind.load = load;
            kind.placeholder = placeholder;
            kind.save = save;
            return kind;
        }

    } // namespace

    void registerExtensions(Extensions &extensions) {
        extensions.components.add(
            componentType<Terrain2DComponent>("terrain2dComponent", encodeTerrain2D, decodeTerrain2D)
        );
        extensions.components.add(
            componentType<Terrain3DComponent>("terrain3dComponent", encodeTerrain3D, decodeTerrain3D)
        );
        extensions.assetKinds.add(terrainKind("terrain2d", TYPE_2D, "Terrain 2D", detect2D, load2D, placeholder2D, save2D));
        extensions.assetKinds.add(terrainKind("terrain3d", TYPE_3D, "Terrain 3D", detect3D, load3D, placeholder3D, save3D));
        extensions.worldSystems.add(worldSystem());
        // World settings: terrain LOD and flora distances — top-level keys of .world.
        extensions.settings.add(settingsType<TerrainLodSettings>(SettingsScope::World, "terrain"));
        extensions.settings.add(settingsType<FloraDistances>(SettingsScope::World, "flora"));
        extensions.resourceProviders.add(Resources::provider());
        extensions.scriptHostApis.add(scriptHostApiTable());
    }

} // namespace Terrain

} // namespace Panda
