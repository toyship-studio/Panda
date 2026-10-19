#include "Panda/Renderer/PrimitiveMeshes.hpp"
#include "Panda/Assets/StaticResources.hpp"
#include "Panda/Renderer/Vertex.hpp"

#include <glm/ext.hpp>
#include <vector>

namespace Panda {

namespace PrimitiveMeshes {

    namespace {

        MeshData pack(const std::vector<Vertex> &vertices, const std::vector<uint32_t> &indices) {
            const uint32_t vertexBufferSize = static_cast<uint32_t>(vertices.size() * sizeof(Vertex));
            MeshData mesh(
                StaticResources::generalVertexLayout,
                Foundation::Memory::copying(vertices.data(), vertexBufferSize),
                vertexBufferSize,
                Foundation::Memory::copying(indices.data(), indices.size() * sizeof(uint32_t)),
                static_cast<uint32_t>(indices.size())
            );
            mesh.vertexStride = sizeof(Vertex);
            return mesh;
        }

        void addQuad(std::vector<uint32_t> &indices, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
            indices.insert(indices.end(), {a, b, c, c, d, a});
        }

        // Sphere/capsule hemispheres: latitude rings [latBegin..latEnd], the centers are offset
        // along Y.
        void addSphereRings(
            std::vector<Vertex> &vertices,
            std::vector<uint32_t> &indices,
            float radius,
            float centerY,
            int rings,
            int sectors,
            float latBegin,
            float latEnd,
            float vBegin,
            float vEnd
        ) {
            const uint32_t base = static_cast<uint32_t>(vertices.size());
            for (int ring = 0; ring <= rings; ring++) {
                const float t = static_cast<float>(ring) / rings;
                const float phi = latBegin + (latEnd - latBegin) * t;
                for (int sector = 0; sector <= sectors; sector++) {
                    const float u = static_cast<float>(sector) / sectors;
                    const float theta = u * glm::two_pi<float>();
                    const glm::vec3 normal(
                        sinf(phi) * cosf(theta), cosf(phi), sinf(phi) * sinf(theta)
                    );
                    vertices.emplace_back(
                        normal * radius + glm::vec3(0.f, centerY, 0.f),
                        glm::vec2(u, vBegin + (vEnd - vBegin) * t), normal,
                        Color(1.f, 1.f, 1.f, 1.f), 1.f, 0
                    );
                }
            }
            for (int ring = 0; ring < rings; ring++) {
                for (int sector = 0; sector < sectors; sector++) {
                    const uint32_t a = base + ring * (sectors + 1) + sector;
                    const uint32_t b = a + sectors + 1;
                    indices.insert(indices.end(), {a, a + 1, b, a + 1, b + 1, b});
                }
            }
        }

    } // namespace

    MeshData buildCube() {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        vertices.reserve(24);
        // A face: the normal + two unit vectors; UV 0..1 per face.
        const auto addFace = [&](const glm::vec3 &normal, const glm::vec3 &right, const glm::vec3 &up) {
            const uint32_t base = static_cast<uint32_t>(vertices.size());
            const glm::vec3 center = normal * 0.5f;
            const glm::vec2 uvs[4] = {{0.f, 1.f}, {1.f, 1.f}, {1.f, 0.f}, {0.f, 0.f}};
            const glm::vec3 corners[4] = {
                center - right * 0.5f - up * 0.5f,
                center + right * 0.5f - up * 0.5f,
                center + right * 0.5f + up * 0.5f,
                center - right * 0.5f + up * 0.5f,
            };
            for (int i = 0; i < 4; i++) {
                vertices.emplace_back(corners[i], uvs[i], normal, Color(1.f, 1.f, 1.f, 1.f), 1.f, 0);
            }
            addQuad(indices, base, base + 1, base + 2, base + 3);
        };
        addFace({0.f, 0.f, 1.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f});   // front
        addFace({0.f, 0.f, -1.f}, {-1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}); // back
        addFace({1.f, 0.f, 0.f}, {0.f, 0.f, -1.f}, {0.f, 1.f, 0.f});  // right
        addFace({-1.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, {0.f, 1.f, 0.f});  // left
        addFace({0.f, 1.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 0.f, -1.f});  // top
        addFace({0.f, -1.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 0.f, 1.f});  // bottom
        return pack(vertices, indices);
    }

    MeshData buildSphere() {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        addSphereRings(vertices, indices, 0.5f, 0.f, 24, 32, 0.f, glm::pi<float>(), 0.f, 1.f);
        return pack(vertices, indices);
    }

    MeshData buildCapsule() {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        constexpr float radius = 0.5f;
        constexpr float halfHeight = 0.5f; // cylindrical part: the hemisphere centers are at ±0.5
        const float halfPi = glm::half_pi<float>();
        // The upper hemisphere, the cylindrical insert (the hemisphere rings coincide at the seam),
        // the lower hemisphere; v is laid out along the surface length.
        addSphereRings(vertices, indices, radius, halfHeight, 12, 32, 0.f, halfPi, 0.f, 0.25f);
        addSphereRings(vertices, indices, radius, -halfHeight, 12, 32, halfPi, glm::pi<float>(), 0.75f, 1.f);
        // The insert: the equator ring of the upper hemisphere is joined with the ring of the
        // lower one.
        const uint32_t base = static_cast<uint32_t>(vertices.size());
        for (int sector = 0; sector <= 32; sector++) {
            const float u = static_cast<float>(sector) / 32;
            const float theta = u * glm::two_pi<float>();
            const glm::vec3 normal(cosf(theta), 0.f, sinf(theta));
            vertices.emplace_back(
                normal * radius + glm::vec3(0.f, halfHeight, 0.f), glm::vec2(u, 0.25f), normal,
                Color(1.f, 1.f, 1.f, 1.f), 1.f, 0
            );
            vertices.emplace_back(
                normal * radius + glm::vec3(0.f, -halfHeight, 0.f), glm::vec2(u, 0.75f), normal,
                Color(1.f, 1.f, 1.f, 1.f), 1.f, 0
            );
        }
        for (int sector = 0; sector < 32; sector++) {
            const uint32_t a = base + sector * 2;
            indices.insert(indices.end(), {a, a + 3, a + 1, a + 3, a, a + 2});
        }
        return pack(vertices, indices);
    }

    MeshData buildPlane() {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        const glm::vec3 normal(0.f, 1.f, 0.f);
        const glm::vec3 corners[4] = {
            {-0.5f, 0.f, -0.5f}, {0.5f, 0.f, -0.5f}, {0.5f, 0.f, 0.5f}, {-0.5f, 0.f, 0.5f}
        };
        const glm::vec2 uvs[4] = {{0.f, 0.f}, {1.f, 0.f}, {1.f, 1.f}, {0.f, 1.f}};
        for (int i = 0; i < 4; i++) {
            vertices.emplace_back(corners[i], uvs[i], normal, Color(1.f, 1.f, 1.f, 1.f), 1.f, 0);
        }
        addQuad(indices, 0, 3, 2, 1);
        return pack(vertices, indices);
    }

    MeshData buildCylinder() {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        constexpr float radius = 0.5f;
        constexpr float halfHeight = 0.5f;
        constexpr int sectors = 32;
        // The side: two rings with radial normals.
        const uint32_t sideBase = static_cast<uint32_t>(vertices.size());
        for (int sector = 0; sector <= sectors; sector++) {
            const float u = static_cast<float>(sector) / sectors;
            const float theta = u * glm::two_pi<float>();
            const glm::vec3 normal(cosf(theta), 0.f, sinf(theta));
            vertices.emplace_back(
                normal * radius + glm::vec3(0.f, halfHeight, 0.f), glm::vec2(u, 0.f), normal,
                Color(1.f, 1.f, 1.f, 1.f), 1.f, 0
            );
            vertices.emplace_back(
                normal * radius + glm::vec3(0.f, -halfHeight, 0.f), glm::vec2(u, 1.f), normal,
                Color(1.f, 1.f, 1.f, 1.f), 1.f, 0
            );
        }
        for (int sector = 0; sector < sectors; sector++) {
            const uint32_t a = sideBase + sector * 2;
            indices.insert(indices.end(), {a, a + 3, a + 1, a + 3, a, a + 2});
        }
        // The caps: center + ring, normals ±Y.
        const auto addCap = [&](float y, const glm::vec3 &normal, bool flip) {
            const uint32_t centerIndex = static_cast<uint32_t>(vertices.size());
            vertices.emplace_back(
                glm::vec3(0.f, y, 0.f), glm::vec2(0.5f, 0.5f), normal, Color(1.f, 1.f, 1.f, 1.f),
                1.f, 0
            );
            for (int sector = 0; sector <= sectors; sector++) {
                const float theta = static_cast<float>(sector) / sectors * glm::two_pi<float>();
                const glm::vec3 direction(cosf(theta), 0.f, sinf(theta));
                vertices.emplace_back(
                    direction * radius + glm::vec3(0.f, y, 0.f),
                    glm::vec2(0.5f) + glm::vec2(direction.x, direction.z) * 0.5f, normal,
                    Color(1.f, 1.f, 1.f, 1.f), 1.f, 0
                );
            }
            for (int sector = 0; sector < sectors; sector++) {
                const uint32_t a = centerIndex + 1 + sector;
                if (flip) {
                    indices.insert(indices.end(), {centerIndex, a, a + 1});
                } else {
                    indices.insert(indices.end(), {centerIndex, a + 1, a});
                }
            }
        };
        addCap(halfHeight, {0.f, 1.f, 0.f}, false);
        addCap(-halfHeight, {0.f, -1.f, 0.f}, true);
        return pack(vertices, indices);
    }

} // namespace PrimitiveMeshes

} // namespace Panda
