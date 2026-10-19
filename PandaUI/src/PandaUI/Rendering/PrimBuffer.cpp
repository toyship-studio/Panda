#include "PandaUI/Rendering/PrimBuffer.hpp"

#include <cassert>

namespace PandaUI {

uint32_t PrimBuffer::vertex(glm::vec2 position, glm::vec2 uv, glm::vec4 color) {
    return vertex(glm::vec3(position, 0.f), uv, color, glm::vec4(0.f));
}

uint32_t PrimBuffer::vertex(glm::vec3 position, glm::vec2 uv, glm::vec4 color) {
    return vertex(position, uv, color, glm::vec4(0.f));
}

uint32_t PrimBuffer::vertex(glm::vec3 position, glm::vec2 uv, glm::vec4 color, glm::vec4 sdfParams) {
    m_drawList->vertices.push_back({position, uv, color, sdfParams});
    assert(m_drawList->vertices.size() < MAX_VERTICES_COUNT && "VERTICES LIMIT REACHED");
    return m_count++;
}

void PrimBuffer::triangle(uint32_t a, uint32_t b, uint32_t c) {
    m_drawList->indices.push_back(m_base + a);
    m_drawList->indices.push_back(m_base + b);
    m_drawList->indices.push_back(m_base + c);
    *m_commandIndexCount += 3;
    assert(m_drawList->indices.size() < MAX_INDICES_COUNT && "INDICES LIMIT REACHED");
}

void PrimBuffer::quad(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    triangle(a, b, c);
    triangle(c, d, a);
}

void PrimBuffer::fan(uint32_t center, uint32_t ringStart, uint32_t ringCount) {
    for (uint32_t i = 0; i < ringCount; ++i) {
        triangle(center, ringStart + i, ringStart + (i + 1) % ringCount);
    }
}

void PrimBuffer::ring(uint32_t outerStart, uint32_t innerStart, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t next = (i + 1) % count;
        quad(outerStart + i, outerStart + next, innerStart + next, innerStart + i);
    }
}

} // namespace PandaUI
