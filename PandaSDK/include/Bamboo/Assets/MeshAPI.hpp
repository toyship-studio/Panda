#pragma once

#include "Bamboo/Base.hpp"

#include <cstdint>
#include <vector>

namespace Bamboo {

// A vertex of a mesh a script builds (MeshAPI::update) for the lit and the PBR shaders.
struct Vertex {
    Vec3 pos;
    Vec2 uv;
    Vec3 normal;
    Vec4 color; // rgb tints the base color of the material (linear); alpha is free for the shader
    // The share of the sky the vertex sees, 0..1: the corner between two walls, the floor of a room
    // under a roof. It dims the light of the sky — the ambient and the environment — and not the
    // sun or the lamps, which have their own shadows. 1 for a mesh in the open.
    float light;
    uint32_t id; // the id the editor picks the mesh by; 0 in a game
};

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

namespace MeshAPI {
    void update(MeshHandle handle, const MeshData &data);
}; // namespace MeshAPI

} // namespace Bamboo