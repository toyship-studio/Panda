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
    // The geometry of a mesh the script created (AssetManagerAPI::createMesh).
    void update(MeshHandle handle, const MeshData &data);
    // The geometry of a mesh asset of the project — a model the script was given by a MeshHandle
    // field: every primitive of it in one index space, as the importer read it (the color of a
    // vertex is COLOR_0 of the model, alpha included). For a script that builds meshes of its own
    // out of models: a voxel world that puts its furniture into the mesh of a chunk. The model
    // is read from its file at every call: read once and keep the data. false — no such mesh.
    bool read(MeshHandle handle, MeshData &data);
}; // namespace MeshAPI

} // namespace Bamboo