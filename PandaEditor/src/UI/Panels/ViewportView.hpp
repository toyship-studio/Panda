#pragma once

#include <PandaUI/PandaUI.hpp>

#include <memory>

namespace Editor {

// Intents from input in the viewport (implemented by the owning panel).
class ViewportViewOutput {
public:
    virtual ~ViewportViewOutput() = default;
    // LMB in the viewport = rect-select (a click without movement = degenerate 1×1 = single selection).
    // start/current — local coordinates (points) of the fixed and the current corner of the rectangle;
    // additive — shift/ctrl/super at the start; committed=true on release. GPU picking via the IDS-FBO
    // is done by the engine.
    virtual void viewportViewDidRectSelect(PandaUI::Point start, PandaUI::Point current, bool additive, bool committed) = 0;
    virtual void viewportViewDidRequestGizmoMode(int mode) = 0; // keys 1/2/3 → 0/1/2
};

// The navigation of the editor camera collected by the Scene surface since the last take: the
// held WASD (while the surface has the focus), the rotation of a right-button drag or a trackpad
// gesture, the zoom of the wheel.
struct SceneNavigation {
    bool forward = false;
    bool backward = false;
    bool left = false;
    bool right = false;
    bool blocked = false; // Shift or Control is held: the keys belong to a shortcut
    PandaUI::Point rotate;
    float zoom = 0.f;
};

// The pointer over the Scene surface, for the gizmo and the viewport tools of the editor parts.
struct ScenePointer {
    PandaUI::Point position;    // window points
    PandaUI::Point local;       // relative to the surface
    bool hovered = false;
    bool leftDown = false;      // the left button is held (it went down over the surface)
    bool leftPressed = false;   // it went down since the last take
};

// The Scene surface: shows the frame of the Scene viewport (the engine feeds its texture) and is
// the input surface of the editor's 3D work — the camera navigation (WASD, right-button drag,
// trackpad, wheel), the gizmo and the tools of the editor parts read the pointer from here, the
// left button starts a rect-select. Nothing of it goes through Panda::Input, which is the game's.
class ViewportView final : public PandaUI::Panel {
public:
    explicit ViewportView(ViewportViewOutput *output);
    // Once a frame, by the engine: what the user did over the surface since the last take (the
    // accumulated rotation and zoom are reset) and where the pointer is.
    SceneNavigation takeNavigation();
    ScenePointer takePointer();
    // Scene FBO texture (seam #1; empty before the migration). flipVertical=true for the engine's
    // render-to-texture (it arrives flipped along Y).
    void setSceneTexture(PandaUI::TextureHandle texture, bool flipVertical = false);
    // The gizmo is "over a handle"/being dragged → the LMB is its (it reads the pointer through
    // takePointer), rect-select does not start.
    void setGizmoActive(bool active) {
        m_gizmoActive = active;
    }
    // A viewport tool (terrain brush etc., EditorExtensions::viewportTools) drives the LMB → it is
    // the tool's (ViewportToolFrame carries the pointer), rect-select does not start. Set by
    // EditorLayer every frame.
    void setToolCapture(bool capture) {
        m_toolCapture = capture;
    }

protected:
    bool pointerDown(PandaUI::PointerEvent &event) override; // focus; LMB — rect-select / gizmo / tool, RMB — rotation
    bool pointerMove(PandaUI::PointerEvent &event) override; // the pointer, the rectangle, the rotation
    bool pointerUp(PandaUI::PointerEvent &event) override;   // release → rect-select commit
    bool scroll(PandaUI::ScrollEvent &event) override;       // trackpad — rotation, wheel — zoom
    bool keyDown(PandaUI::KeyEvent &event) override;         // 1/2/3 — gizmo mode; WASD — flight; the rest is passed through
    bool keyUp(PandaUI::KeyEvent &event) override;
    void focusChanged(bool focused) override;                // the held keys are forgotten with the focus

private:
    PandaUI::Point toLocal(PandaUI::Point screenPoint) const; // screen points → local (relative to the viewport)
    void positionMarquee(PandaUI::Point startScreen, PandaUI::Point currentScreen); // show/move the marquee
    void hideMarquee();

    ViewportViewOutput *m_output;
    std::shared_ptr<PandaUI::ImageView> m_sceneImage; // displays the scene FBO texture
    std::shared_ptr<PandaUI::Label> m_placeholder;    // caption while there is no scene texture
    std::shared_ptr<PandaUI::Panel> m_marquee;        // rect-select rubber-band marquee (hidden outside a drag)
    bool m_pressed = false;                           // LMB is held (rect-select drag capture)
    bool m_dragAdditive = false;                      // shift/ctrl/super at the drag start (adding to the selection)
    bool m_gizmoActive = false;                       // gizmo claims the LMB (over a handle/dragged) → no rect-select
    bool m_toolCapture = false;                       // a viewport tool drives the LMB → no rect-select
    PandaUI::Point m_pressPosition;                   // fixed corner of the rectangle (screen points)
    // The navigation and the pointer since the last take.
    bool m_heldForward = false, m_heldBackward = false, m_heldLeft = false, m_heldRight = false;
    bool m_shiftHeld = false, m_controlHeld = false;
    bool m_rightDown = false; // a right-button drag rotates the camera
    PandaUI::Point m_rotate;
    float m_zoom = 0.f;
    PandaUI::Point m_pointer;
    bool m_leftDown = false;
    bool m_leftPressed = false;
};

} // namespace Editor
