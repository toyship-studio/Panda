#pragma once

#include "PandaUI/Core/Base.hpp"
#include "PandaUI/Core/TextureHandle.hpp"

#include <cstdint>
#include <vector>

namespace PandaUI {

// Cap on the buffers of a single frame — asserted when geometry is written (PrimBuffer).
constexpr uint32_t MAX_VERTICES_COUNT = 100000;
constexpr uint32_t MAX_INDICES_COUNT = 160000;

// Primitive kind of a command — which shader the backend applies.
//  Textured     — the regular path: color * texture (fills, text, images, sharp rectangles).
//  RoundedSDF   — an analytic rounded rectangle via a signed distance field in the shader:
//                 perfect AA at any scale/DPI, fill and stroke from a single distance.
enum class DrawPrimitive : uint8_t {
    Textured,
    RoundedSDF,
};

struct DrawVertex {
    glm::vec3 position;
    glm::vec2 textureCoords; // for RoundedSDF — the local pixel position relative to the shape center
    glm::vec4 color;
    // For RoundedSDF: x,y = half-size (px), z = radius (px), w = stroke width (0 = fill).
    // Unused for Textured.
    glm::vec4 sdfParams = glm::vec4(0.f);
};

struct DrawCommand {
    uint32_t indexOffset = 0;
    uint32_t indexCount = 0;
    TextureHandle texture;
    bool hasClip = false;
    Rect clipRect;
    DrawPrimitive primitive = DrawPrimitive::Textured;
};

struct DrawListView {
    const DrawVertex *vertices = nullptr;
    uint32_t verticesCount = 0;
    const uint32_t *indices = nullptr;
    uint32_t indicesCount = 0;
    const DrawCommand *commands = nullptr;
    uint32_t commandsCount = 0;
    glm::mat4 viewProjection = glm::mat4(1.f);
    Size viewportSize;
    Size dpiScale = Size(1.f, 1.f);
};

struct DrawList {
    std::vector<DrawVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<DrawCommand> commands;
    glm::mat4 viewProjection = glm::mat4(1.f);
    Size viewportSize;
    Size dpiScale = Size(1.f, 1.f);

    void clear() {
        vertices.clear();
        indices.clear();
        commands.clear();
    }

    DrawListView view() const {
        DrawListView result;
        result.vertices = vertices.data();
        result.verticesCount = static_cast<uint32_t>(vertices.size());
        result.indices = indices.data();
        result.indicesCount = static_cast<uint32_t>(indices.size());
        result.commands = commands.data();
        result.commandsCount = static_cast<uint32_t>(commands.size());
        result.viewProjection = viewProjection;
        result.viewportSize = viewportSize;
        result.dpiScale = dpiScale;
        return result;
    }
};

} // namespace PandaUI
