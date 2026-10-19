#pragma once

#include "Panda/Base/Base.hpp"

#include <Miren/Miren.hpp>

#include <cstdint>

namespace Panda {

struct Vertex {
    Vertex(glm::vec3 pos, glm::vec2 uv, glm::vec3 normal, Color color, float light, int32_t id)
        : pos(pos)
        , uv(uv)
        , normal(normal)
        , color(color)
        , light(light)
        , id(id) {}

    Vertex()
        : pos()
        , uv()
        , normal()
        , color()
        , light()
        , id(0) {}

    glm::vec3 pos;
    glm::vec2 uv;
    glm::vec3 normal;
    glm::vec4 color;
    float light;
    uint32_t id;

    inline static Miren::VertexBufferLayoutData createBufferLayoutData() {
        Miren::VertexBufferLayoutData data;
        // pos
        data.pushVec3();
        // uv
        data.pushVec2();
        // normal
        data.pushVec3();
        // color
        data.pushVec4();
        // light
        data.pushFloat(1);
        // object id
        data.pushUInt(1);
        return data;
    }
};

} // namespace Panda