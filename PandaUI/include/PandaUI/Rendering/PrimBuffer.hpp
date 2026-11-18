#pragma once

#include "PandaUI/Rendering/DrawList.hpp"

#include <cstdint>

namespace PandaUI {

// Thin layer for writing the geometry of a single primitive into the DrawList. It hides the buffer
// handling: the index base, local vertex numbering ([0..n)) and tracking of the current command's length.
// The caller defines the geometry (vertices + faces); the layer takes care of the offsets and bookkeeping.
// Created by the renderer after the batch state is synchronized (UIRenderer::beginPrimitive).
class PrimBuffer {
public:
    PrimBuffer() = default; // invalid buffer (for example, everything is clipped away)
    // opacity — of the views the primitive is drawn in (UIRenderer::pushOpacity): the alpha of
    // every vertex is multiplied by it.
    PrimBuffer(DrawList &drawList, uint32_t &commandIndexCount, uint32_t base, float opacity = 1.f)
        : m_drawList(&drawList)
        , m_commandIndexCount(&commandIndexCount)
        , m_base(base)
        , m_opacity(opacity) {}

    explicit operator bool() const {
        return m_drawList != nullptr;
    }

    // Adds a vertex and returns its local index (0, 1, 2, ...).
    uint32_t vertex(glm::vec2 position, glm::vec2 uv, glm::vec4 color);
    uint32_t vertex(glm::vec3 position, glm::vec2 uv, glm::vec4 color);
    // Vertex with SDF parameters (for the RoundedSDF primitive).
    uint32_t vertex(glm::vec3 position, glm::vec2 uv, glm::vec4 color, glm::vec4 sdfParams);
    // Faces by the local vertex indices of this primitive.
    void triangle(uint32_t a, uint32_t b, uint32_t c);
    void quad(uint32_t a, uint32_t b, uint32_t c, uint32_t d);
    // Closed fan around center over the ring [ringStart .. ringStart+ringCount) (fill).
    void fan(uint32_t center, uint32_t ringStart, uint32_t ringCount);
    // Ring strip between an outer and an inner perimeter of the same length (border).
    void ring(uint32_t outerStart, uint32_t innerStart, uint32_t count);

private:
    DrawList *m_drawList = nullptr;
    uint32_t *m_commandIndexCount = nullptr;
    uint32_t m_base = 0;  // global index of the first vertex of the primitive
    uint32_t m_count = 0; // number of vertices written so far
    float m_opacity = 1.f;
};

} // namespace PandaUI
