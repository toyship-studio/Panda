#include "Panda/Terrain/TerrainMesher3D.hpp"
#include "Panda/Assets/StaticResources.hpp"
#include "Panda/Renderer/Vertex.hpp"
#include "Panda/Terrain/MarchingCubesTables.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace Panda {

    // Density for the MESHER: voxels beyond the boundary along X/Z and below are considered solid —
    // the isosurface forms no walls along the map perimeter and no bottom (they are not visible
    // from the world, and on large maps these are thousands of extra triangles). Physics/raycasts
    // use the regular nodeDensity and are not changed.
    float boundaryNodeDensity(const Panda::TerrainData3D &data, int i, int j, int k) {
        int solid = 0;
        for (int dy = -1; dy <= 0; dy++) {
            for (int dx = -1; dx <= 0; dx++) {
                for (int dz = -1; dz <= 0; dz++) {
                    const int x = i + dx;
                    const int y = j + dy;
                    const int z = k + dz;
                    const bool outsideSide = x < 0 || z < 0 ||
                                             x >= static_cast<int>(data.cellsX) ||
                                             z >= static_cast<int>(data.cellsZ);
                    const bool belowBottom = y < 0;
                    if (outsideSide || belowBottom) {
                        // Solid below the ground level of the neighboring column would look like a
                        // wall — the column itself is continued: solid if the in-bounds neighbor is
                        // solid at the same height, otherwise as a continuation (the bottom is
                        // always solid).
                        const int clampedX = std::clamp(x, 0, static_cast<int>(data.cellsX) - 1);
                        const int clampedZ = std::clamp(z, 0, static_cast<int>(data.cellsZ) - 1);
                        const int clampedY = std::max(y, 0);
                        if (belowBottom || data.isSolidAt(clampedX, clampedY, clampedZ)) {
                            solid++;
                        }
                        continue;
                    }
                    if (data.isSolidAt(x, y, z)) { solid++; }
                }
            }
        }
        return solid / 8.f;
    }

namespace TerrainMesher3D {

    namespace {

        // Node field of a chunk with margin 1 (gradients at the boundary). The node (i,j,k) is a
        // voxel corner.
        struct ChunkNodeField {
            static constexpr int MARGIN = 1;
            static constexpr int SIZE = static_cast<int>(TerrainData3D::CHUNK_CELLS) + 1 + MARGIN * 2;

            std::array<float, static_cast<size_t>(SIZE) * SIZE * SIZE> density;

            static int index(int i, int j, int k) {
                return ((j + MARGIN) * SIZE + (i + MARGIN)) * SIZE + (k + MARGIN);
            }

            float d(int i, int j, int k) const {
                return density[index(i, j, k)];
            }

            glm::vec3 gradient(int i, int j, int k) const {
                return glm::vec3(
                    (d(i + 1, j, k) - d(i - 1, j, k)) * 0.5f,
                    (d(i, j + 1, k) - d(i, j - 1, k)) * 0.5f,
                    (d(i, j, k + 1) - d(i, j, k - 1)) * 0.5f
                );
            }

            void fill(const TerrainData3D &data, int startX, int startY, int startZ) {
                const int cells = static_cast<int>(TerrainData3D::CHUNK_CELLS);
                for (int j = -MARGIN; j <= cells + MARGIN; j++) {
                    for (int i = -MARGIN; i <= cells + MARGIN; i++) {
                        for (int k = -MARGIN; k <= cells + MARGIN; k++) {
                            density[index(i, j, k)] =
                                boundaryNodeDensity(data, startX + i, startY + j, startZ + k);
                        }
                    }
                }
            }
        };

        // Weights of the 4 material layers from the 3×3×3 neighborhood — the vertex color
        // (r..a = layers 1..4).
        glm::vec4 materialWeights(const TerrainData3D &data, int x, int y, int z) {
            float weights[TerrainData3D::MATERIAL_COUNT] = {};
            float sum = 0.f;
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    for (int dz = -1; dz <= 1; dz++) {
                        const uint8_t material = data.materialAt(x + dx, y + dy, z + dz);
                        if (material == 0 || material > TerrainData3D::MATERIAL_COUNT) { continue; }
                        weights[material - 1] += 1.f;
                        sum += 1.f;
                    }
                }
            }
            if (sum <= 0.f) { return glm::vec4(1.f, 0.f, 0.f, 0.f); }
            return glm::vec4(weights[0] / sum, weights[1] / sum, weights[2] / sum, weights[3] / sum);
        }

        // Mirrored repeat of the coordinate within an atlas tile: continuous, no wrap seams (from
        // Neverland).
        float mirrorWave(float value) {
            float f = std::fmod(value, 2.f);
            if (f < 0.f) { f += 2.f; }
            return f <= 1.f ? f : 2.f - f;
        }

        // March of a single 16³ section; the result accumulates in the caller's shared buffers —
        // buildColumnMesh merges the sections of a column into a single mesh.
        void appendChunkSection(
            const TerrainData3D &data,
            uint32_t chunkX,
            uint32_t chunkY,
            uint32_t chunkZ,
            std::vector<Vertex> &vertices,
            std::vector<uint32_t> &indices
        );

        MeshData packMesh(const std::vector<Vertex> &vertices, const std::vector<uint32_t> &indices) {
            const uint32_t vertexBufferSize = static_cast<uint32_t>(vertices.size() * sizeof(Vertex));
            Foundation::Memory vertexMemory =
                Foundation::Memory::copying(vertices.data(), vertexBufferSize);
            Foundation::Memory indexMemory =
                Foundation::Memory::copying(indices.data(), indices.size() * sizeof(uint32_t));
            return MeshData(
                StaticResources::generalVertexLayout,
                vertexMemory,
                vertexBufferSize,
                indexMemory,
                static_cast<uint32_t>(indices.size())
            );
        }

    } // namespace

    MeshData
    buildChunkMesh(const TerrainData3D &data, uint32_t chunkX, uint32_t chunkY, uint32_t chunkZ) {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        appendChunkSection(data, chunkX, chunkY, chunkZ, vertices, indices);
        return packMesh(vertices, indices);
    }

    MeshData buildColumnMesh(const TerrainData3D &data, uint32_t chunkX, uint32_t chunkZ) {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        for (uint32_t chunkY = 0; chunkY < data.chunksY(); chunkY++) {
            appendChunkSection(data, chunkX, chunkY, chunkZ, vertices, indices);
        }
        return packMesh(vertices, indices);
    }

    namespace {

        void appendChunkSection(
            const TerrainData3D &data,
            uint32_t chunkX,
            uint32_t chunkY,
            uint32_t chunkZ,
            std::vector<Vertex> &vertices,
            std::vector<uint32_t> &indices
        ) {
            static thread_local ChunkNodeField field;
            const int cells = static_cast<int>(TerrainData3D::CHUNK_CELLS);
            const int startX = static_cast<int>(chunkX) * cells;
            const int startY = static_cast<int>(chunkY) * cells;
            const int startZ = static_cast<int>(chunkZ) * cells;
            field.fill(data, startX, startY, startZ);

            constexpr float ISO = TerrainData3D::ISO;

            for (int y = 0; y < cells; y++) {
                for (int x = 0; x < cells; x++) {
                    for (int z = 0; z < cells; z++) {
                        float cornerDensity[8];
                        int config = 0;
                        for (int corner = 0; corner < 8; corner++) {
                            const int i = x + MarchingCubes::CORNER_OFFSETS[corner][0];
                            const int j = y + MarchingCubes::CORNER_OFFSETS[corner][1];
                            const int k = z + MarchingCubes::CORNER_OFFSETS[corner][2];
                            cornerDensity[corner] = field.d(i, j, k);
                            if (cornerDensity[corner] > ISO) { config |= 1 << corner; }
                        }
                        if (config == 0 || config == 255) { continue; }

                        const glm::vec4 color = materialWeights(data, startX + x, startY + y, startZ + z);

                        const int8_t *tri = MarchingCubes::TRI_TABLE[config];
                        for (int t = 0; tri[t] != -1; t += 3) {
                            glm::vec3 positions[3];
                            glm::vec3 normals[3];
                            for (int vert = 0; vert < 3; vert++) {
                                const int edge = tri[t + vert];
                                const int c0 = MarchingCubes::EDGE_CORNERS[edge][0];
                                const int c1 = MarchingCubes::EDGE_CORNERS[edge][1];
                                const int i0 = x + MarchingCubes::CORNER_OFFSETS[c0][0];
                                const int j0 = y + MarchingCubes::CORNER_OFFSETS[c0][1];
                                const int k0 = z + MarchingCubes::CORNER_OFFSETS[c0][2];
                                const int i1 = x + MarchingCubes::CORNER_OFFSETS[c1][0];
                                const int j1 = y + MarchingCubes::CORNER_OFFSETS[c1][1];
                                const int k1 = z + MarchingCubes::CORNER_OFFSETS[c1][2];
                                const float d0 = cornerDensity[c0];
                                const float d1 = cornerDensity[c1];
                                float lerp = (ISO - d0) / (d1 - d0);
                                lerp = std::clamp(lerp, 0.f, 1.f);
                                positions[vert] = glm::vec3(
                                    (startX + i0 + (i1 - i0) * lerp) * data.cellSize,
                                    (startY + j0 + (j1 - j0) * lerp) * data.cellSize,
                                    (startZ + k0 + (k1 - k0) * lerp) * data.cellSize
                                );
                                const glm::vec3 g0 = field.gradient(i0, j0, k0);
                                const glm::vec3 g1 = field.gradient(i1, j1, k1);
                                glm::vec3 normal = -(g0 + (g1 - g0) * lerp);
                                const float length = glm::length(normal);
                                normals[vert] = length > 1e-4f ? normal / length : glm::vec3(0.f, 1.f, 0.f);
                            }

                            // Degenerate triangles (edges collapsed at the iso level) — skipped.
                            const glm::vec3 crossProduct =
                                glm::cross(positions[1] - positions[0], positions[2] - positions[0]);
                            if (glm::dot(crossProduct, crossProduct) < 1e-10f) { continue; }

                            // UV projection — by the dominant axis of the triangle's average normal.
                            const glm::vec3 avgNormal = (normals[0] + normals[1] + normals[2]) / 3.f;
                            const float absX = std::abs(avgNormal.x);
                            const float absY = std::abs(avgNormal.y);
                            const float absZ = std::abs(avgNormal.z);

                            const uint32_t base = static_cast<uint32_t>(vertices.size());
                            indices.emplace_back(base);
                            indices.emplace_back(base + 1);
                            indices.emplace_back(base + 2);
                            for (int vert = 0; vert < 3; vert++) {
                                const glm::vec3 &position = positions[vert];
                                float u, v;
                                if (absY >= absX && absY >= absZ) {
                                    u = position.x;
                                    v = position.z;
                                } else if (absX >= absZ) {
                                    u = position.z;
                                    v = position.y;
                                } else {
                                    u = position.x;
                                    v = position.y;
                                }
                                const glm::vec2 uv(mirrorWave(u), mirrorWave(v));
                                vertices.emplace_back(Vertex(position, uv, normals[vert], color, 1.f, 0));
                            }
                        }
                    }
                }
            }
        }

    } // namespace

} // namespace TerrainMesher3D

} // namespace Panda
