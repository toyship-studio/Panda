#pragma once

#include "Panda/Renderer/MeshData.hpp"
#include "Panda/Terrain/TerrainData3D.hpp"

#include <cstdint>

namespace Panda {

namespace TerrainMesher3D {

    // Marching cubes mesh of a single chunk (CHUNK_CELLS³ voxels): vertices are in the asset's
    // local meters, normals come from the gradient of the density field, the vertex color holds
    // the weights of the 4 material layers from the 3×3×3 neighborhood (the shader blends the
    // layers). Layout — generalVertexLayout.
    MeshData
    buildChunkMesh(const TerrainData3D &data, uint32_t chunkX, uint32_t chunkY, uint32_t chunkZ);

    // Mesh of the column (chunkX, chunkZ) over the full height: terrain rendering is not split
    // along Y, the vertical sections are merged into a single mesh. Physics chunks in its own
    // way (buildChunkMesh).
    MeshData buildColumnMesh(const TerrainData3D &data, uint32_t chunkX, uint32_t chunkZ);

} // namespace TerrainMesher3D

} // namespace Panda
