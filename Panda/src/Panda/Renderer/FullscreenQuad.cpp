#include "Panda/Renderer/FullscreenQuad.hpp"

#include <Foundation/GraphicsAPI.hpp>

namespace Panda {

FullscreenQuad::FullscreenQuad() {
    Miren::VertexBufferLayoutData layoutData;
    layoutData.pushVec3(); // position
    layoutData.pushVec2(); // uv
    Miren::VertexLayoutHandle layout = Miren::createVertexLayout(layoutData);
    // A fullscreen pass does not change the orientation: the NDC bottom samples the bottom row of
    // the source. The bottom row is v=0 on GL (origin at the bottom), v=1 on D3D (origin at the
    // top).
    constexpr float bottomV = Foundation::ACTIVE_GRAPHICS_TRAITS.originBottomLeft ? 0.f : 1.f;
    constexpr float topV = 1.f - bottomV;
    // clang-format off
    float vertices[20] = {
        -1.f, -1.f, 0.f, 0.f, bottomV,
         1.f, -1.f, 0.f, 1.f, bottomV,
         1.f,  1.f, 0.f, 1.f, topV,
        -1.f,  1.f, 0.f, 0.f, topV,
    };
    // clang-format on
    uint32_t indices[6] = {0, 1, 2, 2, 3, 0};
    m_vertexBuffer = Miren::createVertexBuffer(
        Foundation::Memory::copying(vertices, sizeof(vertices)), sizeof(vertices), layout
    );
    m_indexBuffer = Miren::createIndexBuffer(
        Foundation::Memory::copying(indices, sizeof(indices)), Miren::BufferElementType::UnsignedInt, 6
    );
}

FullscreenQuad::~FullscreenQuad() {
    Miren::deleteVertexBuffer(m_vertexBuffer);
    Miren::deleteIndexBuffer(m_indexBuffer);
}

void FullscreenQuad::submit(Miren::ViewId viewId, Miren::ProgramHandle program) {
    Miren::setVertexBuffer(m_vertexBuffer);
    Miren::setIndexBuffer(m_indexBuffer, 0, 6);
    Miren::submit(viewId, program);
}

} // namespace Panda
