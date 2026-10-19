#pragma once

#include "Panda/Renderer/MeshData.hpp"
#include "Panda/Terrain/TerrainData2D.hpp"

namespace Panda {

namespace TerrainMesher2D {

    // Mesh of a single chunk of the heightmap grid: vertices are in terrain local coordinates
    // (the entity transform moves the terrain as a whole). Layout — generalVertexLayout.
    MeshData buildChunkMesh(const TerrainData2D &data, uint32_t chunkX, uint32_t chunkZ);

} // namespace TerrainMesher2D

} // namespace Panda
