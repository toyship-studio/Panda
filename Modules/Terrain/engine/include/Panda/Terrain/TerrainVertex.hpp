#pragma once

#include <Miren/VertexBufferLayoutData.hpp>
#include <glm/glm.hpp>

namespace Panda {

// Heightmap terrain vertex: a thin format — the layer weights live in the splatmap texture
// (sampled by splatUV), the shader derives the tiling UV of the layer textures from the local
// position.
struct TerrainVertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 splatUV; // [0..1] across the whole map

    TerrainVertex() = default;
    TerrainVertex(glm::vec3 pos, glm::vec3 normal, glm::vec2 splatUV)
        : pos(pos)
        , normal(normal)
        , splatUV(splatUV) {}

    inline static Miren::VertexBufferLayoutData createBufferLayoutData() {
        Miren::VertexBufferLayoutData data;
        // pos
        data.pushVec3();
        // normal
        data.pushVec3();
        // splatUV
        data.pushVec2();
        return data;
    }
};

} // namespace Panda
