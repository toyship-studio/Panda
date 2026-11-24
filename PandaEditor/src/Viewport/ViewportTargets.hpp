#pragma once

#include <Miren/Miren.hpp>
#include <Panda/Base/Base.hpp>

namespace Panda {

// The render targets of the editor viewports (context/architecture.md, "The World view and the Game").
// The world is rendered once per frame through one WorldView, whose targets hold the HDR frame at
// the size of the viewport that renders; the selection mask and the id buffer belong to the Scene;
// every viewport tab owns the output texture its panel shows. Sizes are in points, the pixel size
// follows the dpi of the window.

// The targets of the editor passes over the frame of the Scene (EditorWorldPasses): the selection
// mask the outline is made from and the id buffer of picking.
class EditorPassTargets final {
public:
    EditorPassTargets() = default;
    ~EditorPassTargets();
    EditorPassTargets(const EditorPassTargets &) = delete;
    EditorPassTargets &operator=(const EditorPassTargets &) = delete;

    void init(Size size);
    void resize(Size size); // the same size — nothing happens
    Miren::TextureHandle selectionMask() const {
        return m_selection;
    }
    Miren::FrameBufferHandle idsFrameBuffer() const {
        return m_idsFB;
    }
    // The passes as views of the frame being built; both clear their targets — no selection, no
    // mask; nothing under the pointer, id 0.
    Miren::ViewId addSelectionView() const;
    Miren::ViewId addIdsView() const;
    // How the composite reads the mask (ViewportComposite::draw): the width of the outline and a
    // texel of the mask, in its uv.
    glm::vec4 outlineStep() const;

private:
    void createFrameBuffers();
    void deleteFrameBuffers();

    Size m_size;
    Miren::TextureHandle m_selection, m_ids, m_idsDepth;
    Miren::FrameBufferHandle m_selectionFB, m_idsFB;
    bool m_initialized = false;
};

// The fullscreen pass that composes the output of a viewport: the color of the frame with the
// outline of the selection mask. One program and one quad serve every surface.
class ViewportComposite final {
public:
    ViewportComposite() = default;
    ~ViewportComposite();
    ViewportComposite(const ViewportComposite &) = delete;
    ViewportComposite &operator=(const ViewportComposite &) = delete;

    void init();
    // Into `view` — a view over the output of a surface (ViewportSurface::addView). outlineStep —
    // EditorPassTargets::outlineStep of the mask; zero with the empty mask.
    void draw(
        Miren::ViewId view, Miren::TextureHandle color, Miren::TextureHandle selectionMask, const glm::vec4 &outlineStep
    ) const;
    // A 1×1 transparent mask: no outline (the Game viewport).
    Miren::TextureHandle emptyMask() const {
        return m_emptyMask;
    }

private:
    Miren::VertexBufferHandle m_vertexBuffer;
    Miren::IndexBufferHandle m_indexBuffer;
    Miren::ProgramHandle m_program;
    Miren::TextureHandle m_emptyMask;
    bool m_initialized = false;
};

// The output of one viewport tab: an RGBA8 texture at the frame of the tab, shown by its panel.
class ViewportSurface final {
public:
    ViewportSurface() = default;
    ~ViewportSurface();
    ViewportSurface(const ViewportSurface &) = delete;
    ViewportSurface &operator=(const ViewportSurface &) = delete;

    void init(Size size);
    void setFrame(Rect frame); // a new size resizes the output
    const Rect &frame() const {
        return m_frame;
    }
    bool isReady() const { // the panel's layout gave the tab a size
        return m_frame.size.width >= 1.f && m_frame.size.height >= 1.f;
    }
    Miren::TextureHandle result() const {
        return m_result;
    }
    // The size of the output texture in pixels (the frame times the scale of the window).
    uint32_t pixelWidth() const {
        return m_pixelWidth;
    }
    uint32_t pixelHeight() const {
        return m_pixelHeight;
    }
    // The pass that composes the output, as a view of the frame being built: it clears the output.
    Miren::ViewId addView() const;
    // The framebuffer over the output: who draws on top of the composed frame (the game UI) makes
    // a view of its own over it.
    Miren::FrameBufferHandle frameBuffer() const {
        return m_fb;
    }

private:
    Rect m_frame;
    Miren::TextureHandle m_result;
    Miren::FrameBufferHandle m_fb;
    uint32_t m_pixelWidth = 0;
    uint32_t m_pixelHeight = 0;
    bool m_initialized = false;
};

} // namespace Panda
