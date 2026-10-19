#pragma once

#include "Panda/Renderer/MeshData.hpp"
#include "Panda/Terrain/TerrainData2D.hpp"
#include "Panda/Terrain/TerrainData3D.hpp"

#include <glm/glm.hpp>

namespace Panda {

// Far LOD meshes of the terrain: a decimated heightfield of the top surface with skirts along the
// perimeter (they cover the seams between levels). 3d — a vertical column of chunks
// (interiors/caves are not drawn in the distance — accepted), 2d — a chunk. Vertex weights —
// the pure layer of the top voxel.
namespace TerrainMesherLod {

    // Depth of the skirt going down along the mesh perimeter. The edge of the more detailed level
    // follows the actual heights, while the edge of the neighboring coarser one runs in straight
    // segments through every other node, so a T-junction remains between them. The skirt covers
    // it. A single constant for chunks and tiles: different depths would leave a gap at the
    // core → tier 1 transition.
    constexpr float SKIRT_DEPTH = 3.f;

    // Tile of an LOD ring: a region of columns (3d) / chunks (2d) [x0, x0+w) × [z0, z0+d) as a
    // single mesh; step — the grid step in cells (must divide CHUNK_CELLS). Cell columns without
    // a surface are drawn with height 0.
    MeshData buildRegionLod3D(
        const TerrainData3D &data,
        uint32_t columnX0,
        uint32_t columnZ0,
        uint32_t columnsW,
        uint32_t columnsD,
        uint32_t step
    );
    MeshData buildRegionLod2D(
        const TerrainData2D &data,
        uint32_t chunkX0,
        uint32_t chunkZ0,
        uint32_t chunksW,
        uint32_t chunksD,
        uint32_t step
    );

    // Sample of a far grid node (coordinates — a data cell/vertex, clamped at the boundary).
    struct SurfaceSample {
        float height = 0.f;
        uint8_t layer = 1;
    };
    SurfaceSample sampleFar3D(const TerrainData3D &data, int cellX, int cellZ);
    SurfaceSample sampleFar2D(const TerrainData2D &data, int vertexX, int vertexZ);

} // namespace TerrainMesherLod

} // namespace Panda
