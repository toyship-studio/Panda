#include "Panda/Terrain/TerrainMesherLod.hpp"
#include "Panda/Assets/StaticResources.hpp"
#include "Panda/Terrain/TerrainResources.hpp"
#include "Panda/Renderer/Vertex.hpp"
#include "Panda/Terrain/TerrainVertex.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace Panda {

namespace TerrainMesherLod {

    namespace {

        constexpr float UV_METERS_PER_TILE = 4.f; // scale of the layer textures of the 3d path

        glm::vec4 layerWeights(uint8_t layer) {
            switch (layer) {
                case 2: return glm::vec4(0.f, 1.f, 0.f, 0.f);
                case 3: return glm::vec4(0.f, 0.f, 1.f, 0.f);
                case 4: return glm::vec4(0.f, 0.f, 0.f, 1.f);
                default: return glm::vec4(1.f, 0.f, 0.f, 0.f);
            }
        }

        // 3d path: UV by mirrored repeat (consistent with the chunk MC mesher and
        // the atlas shaders on top of it).
        float mirrorWave(float value) {
            float f = std::fmod(value, 2.f);
            if (f < 0.f) { f += 2.f; }
            return f <= 1.f ? f : 2.f - f;
        }

        // Common triangulator of a heightfield grid with skirts: a rectangular grid
        // cellsGX×cellsGZ, the samples come as a ready padded lattice ((cellsGX+3)×(cellsGZ+3), a
        // margin of ±1 node for the normals). The vertex format is defined by
        // makeVertex(position, normal, sample) — the 2d path builds thin TerrainVertex (weights in
        // the splatmap), the 3d path — Vertex with layer weights.
        template<typename VertexT, typename MakeVertex>
        MeshData triangulateGrid(
            float originX,
            float originZ,
            float stepMeters,
            uint32_t cellsGX,
            uint32_t cellsGZ,
            const SurfaceSample *paddedSamples,
            Miren::VertexLayoutHandle layout,
            const MakeVertex &makeVertex
        ) {
            const uint32_t pointsX = cellsGX + 1;
            const uint32_t pointsZ = cellsGZ + 1;
            const uint32_t paddedX = pointsX + 2;
            auto sample = [&](int gx, int gz) -> const SurfaceSample & {
                return paddedSamples[static_cast<size_t>(gz + 1) * paddedX + (gx + 1)];
            };

            std::vector<VertexT> vertices;
            std::vector<uint32_t> indices;
            vertices.reserve(static_cast<size_t>(pointsX) * pointsZ * 2);
            auto addTriangle = [&](uint32_t a, uint32_t b, uint32_t c) {
                indices.emplace_back(a);
                indices.emplace_back(b);
                indices.emplace_back(c);
            };

            struct GridPoint {
                glm::vec3 position;
                glm::vec3 normal;
                SurfaceSample sample;
            };
            std::vector<GridPoint> grid(static_cast<size_t>(pointsX) * pointsZ);
            std::vector<uint32_t> topIndices(grid.size());
            for (uint32_t gz = 0; gz < pointsZ; gz++) {
                for (uint32_t gx = 0; gx < pointsX; gx++) {
                    const float x = originX + gx * stepMeters;
                    const float z = originZ + gz * stepMeters;
                    const SurfaceSample &center = sample(gx, gz);
                    const glm::vec3 normal = glm::normalize(glm::vec3(
                        (sample(gx - 1, gz).height - sample(gx + 1, gz).height) / (2.f * stepMeters),
                        1.f,
                        (sample(gx, gz - 1).height - sample(gx, gz + 1).height) / (2.f * stepMeters)
                    ));
                    GridPoint &point = grid[static_cast<size_t>(gz) * pointsX + gx];
                    point.position = glm::vec3(x, center.height, z);
                    point.normal = normal;
                    point.sample = center;
                    vertices.emplace_back(makeVertex(point.position, point.normal, point.sample));
                    topIndices[static_cast<size_t>(gz) * pointsX + gx] =
                        static_cast<uint32_t>(vertices.size() - 1);
                }
            }
            // The top grid (winding as in the chunk meshers: the front face is up).
            for (uint32_t gz = 0; gz < cellsGZ; gz++) {
                for (uint32_t gx = 0; gx < cellsGX; gx++) {
                    const uint32_t v00 = topIndices[static_cast<size_t>(gz) * pointsX + gx];
                    const uint32_t v01 = topIndices[static_cast<size_t>(gz + 1) * pointsX + gx];
                    const uint32_t v10 = topIndices[static_cast<size_t>(gz) * pointsX + gx + 1];
                    const uint32_t v11 = topIndices[static_cast<size_t>(gz + 1) * pointsX + gx + 1];
                    addTriangle(v00, v01, v11);
                    addTriangle(v11, v10, v00);
                }
            }

            // Skirts along the perimeter: a ribbon going down, DOUBLE-SIDED. A one-sided ribbon
            // covers only half of the T-junctions: its face looks inside its own mesh, so with
            // MIREN_STATE_CULL_FACE only the skirt of the mesh nearer to the camera is visible. It
            // hangs down from its edge and covers the hole only when the near edge is HIGHER than
            // the far one; where the far edge is higher, the hole lies above the edge of the near
            // one, and only the skirt of the far mesh could cover it — but that one is culled. This
            // caused hairline gaps of 1–2 pixels at the seams. Flipping the winding would only swap
            // the halves, so the ribbon is drawn from both sides (the same vertices, +6 indices).
            auto addSkirt = [&](const GridPoint &a, const GridPoint &b, const glm::vec3 &outNormal) {
                const glm::vec3 drop(0.f, SKIRT_DEPTH, 0.f);
                vertices.emplace_back(makeVertex(a.position, outNormal, a.sample));
                const uint32_t topA = static_cast<uint32_t>(vertices.size() - 1);
                vertices.emplace_back(makeVertex(b.position, outNormal, b.sample));
                const uint32_t topB = static_cast<uint32_t>(vertices.size() - 1);
                vertices.emplace_back(makeVertex(a.position - drop, outNormal, a.sample));
                const uint32_t bottomA = static_cast<uint32_t>(vertices.size() - 1);
                vertices.emplace_back(makeVertex(b.position - drop, outNormal, b.sample));
                const uint32_t bottomB = static_cast<uint32_t>(vertices.size() - 1);
                addTriangle(topA, bottomB, bottomA);
                addTriangle(topA, topB, bottomB);
                addTriangle(topA, bottomA, bottomB); // the back side of the same quads
                addTriangle(topA, bottomB, topB);
            };
            const auto point = [&](uint32_t gx, uint32_t gz) -> const GridPoint & {
                return grid[static_cast<size_t>(gz) * pointsX + gx];
            };
            for (uint32_t gx = 0; gx < cellsGX; gx++) {
                addSkirt(point(gx + 1, 0), point(gx, 0), glm::vec3(0.f, 0.f, -1.f)); // north edge
                addSkirt(point(gx, cellsGZ), point(gx + 1, cellsGZ), glm::vec3(0.f, 0.f, 1.f)); // south
            }
            for (uint32_t gz = 0; gz < cellsGZ; gz++) {
                addSkirt(point(0, gz), point(0, gz + 1), glm::vec3(-1.f, 0.f, 0.f)); // west edge
                addSkirt(point(cellsGX, gz + 1), point(cellsGX, gz), glm::vec3(1.f, 0.f, 0.f)); // east
            }

            const uint32_t vertexBufferSize = static_cast<uint32_t>(vertices.size() * sizeof(VertexT));
            Foundation::Memory vertexMemory =
                Foundation::Memory::copying(vertices.data(), vertexBufferSize);
            Foundation::Memory indexMemory =
                Foundation::Memory::copying(indices.data(), indices.size() * sizeof(uint32_t));
            return MeshData(
                layout,
                vertexMemory,
                vertexBufferSize,
                indexMemory,
                static_cast<uint32_t>(indices.size())
            );
        }

        // A rectangular grid with a sample callback (ring tiles): gathers the padded lattice and
        // triangulates it with the given vertex format.
        template<typename VertexT, typename MakeVertex>
        MeshData buildGrid(
            float originX,
            float originZ,
            float stepMeters,
            uint32_t cellsGX,
            uint32_t cellsGZ,
            const std::function<SurfaceSample(int, int)> &sampleAt,
            Miren::VertexLayoutHandle layout,
            const MakeVertex &makeVertex
        ) {
            const uint32_t paddedX = cellsGX + 3;
            const uint32_t paddedZ = cellsGZ + 3;
            std::vector<SurfaceSample> samples(static_cast<size_t>(paddedX) * paddedZ);
            for (int gz = -1; gz <= static_cast<int>(cellsGZ) + 1; gz++) {
                for (int gx = -1; gx <= static_cast<int>(cellsGX) + 1; gx++) {
                    samples[static_cast<size_t>(gz + 1) * paddedX + (gx + 1)] = sampleAt(gx, gz);
                }
            }
            return triangulateGrid<VertexT>(
                originX, originZ, stepMeters, cellsGX, cellsGZ, samples.data(), layout, makeVertex
            );
        }

    } // namespace

    SurfaceSample sampleFar3D(const TerrainData3D &data, int cellX, int cellZ) {
        // A direct scan of the column from the top (instead of field samples — the cost is
        // critical: it is built on the main thread). The deviation from the MC surface is ≤ half a
        // voxel — in the distance it is hidden by the grid step and the skirts.
        const int voxelX = std::clamp(cellX, 0, static_cast<int>(data.cellsX) - 1);
        const int voxelZ = std::clamp(cellZ, 0, static_cast<int>(data.cellsZ) - 1);
        SurfaceSample sample;
        for (int y = static_cast<int>(data.cellsY) - 1; y >= 0; y--) {
            const uint8_t layer = data.materialAt(voxelX, y, voxelZ);
            if (layer != 0) {
                sample.height = (y + 1) * data.cellSize;
                sample.layer = layer;
                return sample;
            }
        }
        return sample;
    }

    SurfaceSample sampleFar2D(const TerrainData2D &data, int vertexX, int vertexZ) {
        const uint32_t x = static_cast<uint32_t>(std::clamp(vertexX, 0, static_cast<int>(data.cellsX)));
        const uint32_t z = static_cast<uint32_t>(std::clamp(vertexZ, 0, static_cast<int>(data.cellsZ)));
        SurfaceSample sample;
        sample.height = data.heightAt(x, z);
        sample.layer = data.layerAt(x, z);
        return sample;
    }

    MeshData buildRegionLod3D(
        const TerrainData3D &data,
        uint32_t columnX0,
        uint32_t columnZ0,
        uint32_t columnsW,
        uint32_t columnsD,
        uint32_t step
    ) {
        const float columnMeters = TerrainData3D::CHUNK_CELLS * data.cellSize;
        const uint32_t stepCells = std::max(1u, step);
        const uint32_t cellsGX = columnsW * TerrainData3D::CHUNK_CELLS / stepCells;
        const uint32_t cellsGZ = columnsD * TerrainData3D::CHUNK_CELLS / stepCells;
        const int baseVoxelX = static_cast<int>(columnX0 * TerrainData3D::CHUNK_CELLS);
        const int baseVoxelZ = static_cast<int>(columnZ0 * TerrainData3D::CHUNK_CELLS);
        auto sampleAt = [&](int gridX, int gridZ) {
            return sampleFar3D(
                data, baseVoxelX + gridX * static_cast<int>(stepCells),
                baseVoxelZ + gridZ * static_cast<int>(stepCells)
            );
        };
        const auto makeVertex = [](const glm::vec3 &position, const glm::vec3 &normal,
                                   const SurfaceSample &sample) {
            const glm::vec2 uv(
                mirrorWave(position.x / UV_METERS_PER_TILE), mirrorWave(position.z / UV_METERS_PER_TILE)
            );
            return Vertex(position, uv, normal, layerWeights(sample.layer), 1.f, 0);
        };
        return buildGrid<Vertex>(
            columnX0 * columnMeters, columnZ0 * columnMeters, stepCells * data.cellSize, cellsGX,
            cellsGZ, sampleAt, StaticResources::generalVertexLayout, makeVertex
        );
    }

    MeshData buildRegionLod2D(
        const TerrainData2D &data,
        uint32_t chunkX0,
        uint32_t chunkZ0,
        uint32_t chunksW,
        uint32_t chunksD,
        uint32_t step
    ) {
        const float chunkMeters = TerrainData2D::CHUNK_CELLS * data.cellSize;
        const uint32_t stepCells = std::max(1u, step);
        const uint32_t cellsGX = chunksW * TerrainData2D::CHUNK_CELLS / stepCells;
        const uint32_t cellsGZ = chunksD * TerrainData2D::CHUNK_CELLS / stepCells;
        const int baseVertexX = static_cast<int>(chunkX0 * TerrainData2D::CHUNK_CELLS);
        const int baseVertexZ = static_cast<int>(chunkZ0 * TerrainData2D::CHUNK_CELLS);
        auto sampleAt = [&](int gridX, int gridZ) {
            return sampleFar2D(
                data, baseVertexX + gridX * static_cast<int>(stepCells),
                baseVertexZ + gridZ * static_cast<int>(stepCells)
            );
        };
        const float cellSize = data.cellSize;
        const auto makeVertex = [&data, cellSize](
                                    const glm::vec3 &position, const glm::vec3 &normal,
                                    const SurfaceSample &
                                ) {
            return TerrainVertex(
                position, normal, data.splatUV(position.x / cellSize, position.z / cellSize)
            );
        };
        return buildGrid<TerrainVertex>(
            chunkX0 * chunkMeters, chunkZ0 * chunkMeters, stepCells * data.cellSize, cellsGX,
            cellsGZ, sampleAt, Terrain::Resources::vertexLayout, makeVertex
        );
    }

} // namespace TerrainMesherLod

} // namespace Panda
