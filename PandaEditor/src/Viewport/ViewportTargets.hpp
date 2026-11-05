#pragma once

#include <Miren/Miren.hpp>
#include <Panda/Base/Base.hpp>

namespace Panda {

// The render targets of the editor viewports (context/architecture.md, "The Scene and the Game").
// The world is rendered once per frame, into one set of scene targets sized to the viewport that
// renders this frame; the selection mask and the id buffer belong to the Scene; every viewport tab
// owns the output texture its panel shows. Sizes are in points, the pixel size follows the dpi of
// the window.

// The HDR frame of the world (SCENE_VIEW: color, emissive, depth), the selection mask of the
// outline (SELECTED_GEOMETRY_VIEW) and the id buffer of picking (IDS_VIEW).
class SceneRenderTargets final {
public:
    SceneRenderTargets() = default;
    ~SceneRenderTargets();
    SceneRenderTargets(const SceneRenderTargets &) = delete;
    SceneRenderTargets &operator=(const SceneRenderTargets &) = delete;

    void init(Size size);
    void resize(Size size); // the same size — nothing happens
    Size size() const {
        return m_size;
    }
    Miren::TextureHandle color() const {
        return m_color;
    }
    Miren::TextureHandle emissive() const {
        return m_emissive;
    }
    Miren::TextureHandle depth() const {
        return m_depth;
    }
    Miren::TextureHandle selectionMask() const {
        return m_selection;
    }
    Miren::FrameBufferHandle idsFrameBuffer() const {
        return m_idsFB;
    }

private:
    void createFrameBuffers();
    void deleteFrameBuffers();
    void bindViews();

    Size m_size;
    Miren::TextureHandle m_color, m_emissive, m_depth, m_selection, m_ids, m_idsDepth;
    Miren::FrameBufferHandle m_sceneFB, m_selectionFB, m_idsFB;
    bool m_initialized = false;
};

// The fullscreen pass that composes the output of a viewport (EDITOR_VIEWPORT_VIEW): the color of
// the frame with the outline of the selection mask. One program and one quad serve every surface.
class ViewportComposite final {
public:
    ViewportComposite() = default;
    ~ViewportComposite();
    ViewportComposite(const ViewportComposite &) = delete;
    ViewportComposite &operator=(const ViewportComposite &) = delete;

    void init();
    void draw(Miren::TextureHandle color, Miren::TextureHandle selectionMask) const;
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
    // A view draws into this output this frame (EDITOR_VIEWPORT_VIEW — the composite of the frame,
    // GAME_UI_VIEW — the game UI on top of it). The state of a Miren view is a snapshot per frame.
    void bindView(Miren::ViewId view) const;

private:
    Rect m_frame;
    Miren::TextureHandle m_result;
    Miren::FrameBufferHandle m_fb;
    bool m_initialized = false;
};

} // namespace Panda
