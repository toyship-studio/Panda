#pragma once

#include <Miren/Miren.hpp>

namespace Panda {

// The single fullscreen quad of the post effects (ingredient model: an effect receives the
// infrastructure and holds only its own targets). The UV orientation accounts for the backend
// origin (GL — bottom, D3D — top): duplicates of this quad with a hardcoded V have already
// led to a flipped frame. Owner — PostprocessingPipeline; bloom and exposure receive the
// quad as a parameter of apply().
class FullscreenQuad final {
public:
    FullscreenQuad();
    ~FullscreenQuad();

    FullscreenQuad(const FullscreenQuad &) = delete;
    FullscreenQuad &operator=(const FullscreenQuad &) = delete;

    // setVertexBuffer + setIndexBuffer + submit(viewId, program).
    void submit(Miren::ViewId viewId, Miren::ProgramHandle program);

private:
    Miren::VertexBufferHandle m_vertexBuffer;
    Miren::IndexBufferHandle m_indexBuffer;
};

} // namespace Panda
