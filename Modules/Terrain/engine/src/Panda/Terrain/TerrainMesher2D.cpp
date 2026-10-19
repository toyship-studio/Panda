#include "Panda/Terrain/TerrainMesher2D.hpp"
#include "Panda/Terrain/TerrainResources.hpp"
#include "Panda/Terrain/TerrainMesherLod.hpp"
#include "Panda/Terrain/TerrainVertex.hpp"

#include <algorithm>
#include <vector>

namespace Panda {

namespace TerrainMesher2D {


    MeshData buildChunkMesh(const TerrainData2D &data, uint32_t chunkX, uint32_t chunkZ) {
        const uint32_t startX = chunkX * TerrainData2D::CHUNK_CELLS;
        const uint32_t startZ = chunkZ * TerrainData2D::CHUNK_CELLS;
        const uint32_t cellsX = std::min(TerrainData2D::CHUNK_CELLS, data.cellsX - startX);
        const uint32_t cellsZ = std::min(TerrainData2D::CHUNK_CELLS, data.cellsZ - startZ);

        std::vector<TerrainVertex> vertices;
        // The top grid + 4 vertices per segment of the perimeter skirt.
        vertices.reserve(
            static_cast<size_t>(cellsX + 1) * (cellsZ + 1) + 8u * (cellsX + cellsZ)
        );
        std::vector<uint32_t> indices;
        indices.reserve(static_cast<size_t>(cellsX) * cellsZ * 6 + 24u * (cellsX + cellsZ));

        // Thin vertex: the layer weights are in the splatmap (splatUV [0..1] across the map), the
        // tiling UV of the textures is derived by the shader from the local position.
        for (uint32_t z = 0; z <= cellsZ; z++) {
            for (uint32_t x = 0; x <= cellsX; x++) {
                const uint32_t vertexX = startX + x;
                const uint32_t vertexZ = startZ + z;
                const glm::vec3 position(
                    vertexX * data.cellSize, data.heightAt(vertexX, vertexZ), vertexZ * data.cellSize
                );
                vertices.emplace_back(
                    position, data.normalAt(vertexX, vertexZ),
                    data.splatUV(static_cast<float>(vertexX), static_cast<float>(vertexZ))
                );
            }
        }

        const uint32_t rowStride = cellsX + 1;
        for (uint32_t z = 0; z < cellsZ; z++) {
            for (uint32_t x = 0; x < cellsX; x++) {
                const uint32_t v00 = z * rowStride + x;
                const uint32_t v10 = v00 + 1;
                const uint32_t v01 = v00 + rowStride;
                const uint32_t v11 = v01 + 1;
                // Winding as in the Neverland heightfield LOD: the front face is up.
                for (uint32_t index : {v00, v01, v11, v11, v10, v00}) {
                    indices.emplace_back(index);
                }
            }
        }

        // Skirts along the perimeter — under the same contract as for the LOD tiles. The chunk edge
        // goes through every vertex, the edge of the neighboring tier 1 tile — through every other
        // one, as a straight segment. In concave places the gap is covered by the tile skirt, but
        // in convex ones the chunk edge is above the segment, and without its own skirt single
        // pixels showed through there. The chunk↔chunk joint does not suffer from this: there the
        // vertices are shared bitwise, the skirt goes under the neighbor's surface.
        const auto gridVertex = [&](uint32_t x, uint32_t z) {
            return vertices[static_cast<size_t>(z) * rowStride + x]; // a copy: the vector grows below
        };
        const auto addSkirt = [&](TerrainVertex a, TerrainVertex b, const glm::vec3 &outNormal) {
            const glm::vec3 drop(0.f, TerrainMesherLod::SKIRT_DEPTH, 0.f);
            const uint32_t topA = static_cast<uint32_t>(vertices.size());
            vertices.emplace_back(a.pos, outNormal, a.splatUV);
            const uint32_t topB = static_cast<uint32_t>(vertices.size());
            vertices.emplace_back(b.pos, outNormal, b.splatUV);
            const uint32_t bottomA = static_cast<uint32_t>(vertices.size());
            vertices.emplace_back(a.pos - drop, outNormal, a.splatUV);
            const uint32_t bottomB = static_cast<uint32_t>(vertices.size());
            vertices.emplace_back(b.pos - drop, outNormal, b.splatUV);
            // Both sides: see TerrainMesherLod — a one-sided skirt is culled on the side of the
            // far mesh and leaves a gap where the far edge is higher.
            for (uint32_t index :
                 {topA, bottomB, bottomA, topA, topB, bottomB,
                  topA, bottomA, bottomB, topA, bottomB, topB}) {
                indices.emplace_back(index);
            }
        };
        for (uint32_t x = 0; x < cellsX; x++) {
            addSkirt(gridVertex(x + 1, 0), gridVertex(x, 0), glm::vec3(0.f, 0.f, -1.f));
            addSkirt(gridVertex(x, cellsZ), gridVertex(x + 1, cellsZ), glm::vec3(0.f, 0.f, 1.f));
        }
        for (uint32_t z = 0; z < cellsZ; z++) {
            addSkirt(gridVertex(0, z), gridVertex(0, z + 1), glm::vec3(-1.f, 0.f, 0.f));
            addSkirt(gridVertex(cellsX, z + 1), gridVertex(cellsX, z), glm::vec3(1.f, 0.f, 0.f));
        }

        const uint32_t vertexBufferSize =
            static_cast<uint32_t>(vertices.size() * sizeof(TerrainVertex));
        Foundation::Memory vertexMemory = Foundation::Memory::copying(vertices.data(), vertexBufferSize);
        Foundation::Memory indexMemory =
            Foundation::Memory::copying(indices.data(), indices.size() * sizeof(uint32_t));
        return MeshData(
            Terrain::Resources::vertexLayout,
            vertexMemory,
            vertexBufferSize,
            indexMemory,
            static_cast<uint32_t>(indices.size())
        );
    }

} // namespace TerrainMesher2D

} // namespace Panda
