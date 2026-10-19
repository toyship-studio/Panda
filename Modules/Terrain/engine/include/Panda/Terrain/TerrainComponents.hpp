#pragma once

#include "Panda/Assets/Base/AssetRef.hpp"

namespace Panda {

// Scene wiring: a reference to the asset + overrides of the placement. The landscape data (heights,
// layers, FLORA) lives in the .terrain itself — it is edited in the asset tab (the document model).
struct Terrain2DComponent final {
    AssetRef<Asset> terrain;  // Terrain2DAsset (.terrain, heightmap), empty = not assigned
    AssetRef<Asset> material; // empty = Terrain::Resources::material2D
    bool physicsEnabled = true; // static heightfield collider in Physics3D

    Terrain2DComponent() = default;
    Terrain2DComponent(const Terrain2DComponent &other) = default;
};

struct Terrain3DComponent final {
    AssetRef<Asset> terrain;  // Terrain3DAsset (.terrain, voxels), empty = not assigned
    AssetRef<Asset> material; // empty = Terrain::Resources::material3D
    bool physicsEnabled = true; // static per-chunk mesh colliders in Physics3D

    Terrain3DComponent() = default;
    Terrain3DComponent(const Terrain3DComponent &other) = default;
};

} // namespace Panda
