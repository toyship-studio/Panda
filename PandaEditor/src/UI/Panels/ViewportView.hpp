#pragma once

#include "UI/Widgets/ViewportVeil.hpp"

#include <PandaUI/PandaUI.hpp>

#include <memory>

namespace Editor {

// The selection gesture of the left button over the Scene, step by step.
enum class RectSelectPhase {
    Stretch, // the rectangle changed; the press is the first one, 1×1 — it picks what is under the pointer
    Commit,  // the button was released: the last rectangle of the gesture
    Drop,    // the gesture ended without a last rectangle (the surface changed under it): what it
             // selected stays, nothing more is read
};

// Intents from input in the viewport (implemented by the owning panel).
class ViewportViewOutput {
public:
    virtual ~ViewportViewOutput() = default;
    // LMB in the viewport selects. A press picks the entity under the pointer (a 1×1 rectangle),
    // and a press that never leaves the click slop stays that — a click selects one entity. A
    // drag past the slop stretches a rectangle from the press point. start/current — local
    // coordinates (points) of the fixed and the current corner; additive — shift/ctrl/super at
    // the press. GPU picking via the IDS-FBO is done by the engine.
    virtual void viewportViewDidRectSelect(
        PandaUI::Point start, PandaUI::Point current, bool additive, RectSelectPhase phase
    ) = 0;
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
    bool snap = false;          // the primary key is held (⌘ on macOS, Ctrl elsewhere): the gizmo goes by steps
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
    // The frame of the Scene as a texture. flipVertical=true for a render target whose origin is
    // at the bottom left (it arrives flipped along Y).
    void setSceneTexture(PandaUI::TextureHandle texture, bool flipVertical = false);
    // false — the Game renders and the Scene keeps its last frame: the veil says so.
    void setLive(bool live) {
        m_veil->setShown(!live);
    }
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
    // A selection gesture is in flight (the left button is held since a press that selects).
    // What follows the selection by changing the layout — a tab that opens next to the viewport
    // — waits for its end: the rest of the gesture must see the picture the press saw.
    bool isSelecting() const {
        return m_pressed;
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
    // The surface moved or changed its size under the gesture: the rectangle no longer lies over
    // the picture it was stretched on. The gesture is dropped (true).
    bool dropGestureIfFrameChanged();

    ViewportViewOutput *m_output;
    std::shared_ptr<PandaUI::ImageView> m_sceneImage; // displays the scene FBO texture
    std::shared_ptr<PandaUI::Label> m_placeholder;    // caption while there is no scene texture
    std::shared_ptr<PandaUI::Panel> m_marquee;        // rect-select rubber-band marquee (hidden outside a drag)
    std::shared_ptr<ViewportVeil> m_veil;             // over the frame while the Game has the focus
    bool m_pressed = false;                           // LMB is held (rect-select drag capture)
    bool m_dragging = false;                          // the press left the click slop: it stretches a rectangle
    bool m_dragAdditive = false;                      // shift/ctrl/super at the drag start (adding to the selection)
    PandaUI::Rect m_pressFrame;                       // the frame of the surface at the press (the gesture lives in it)
    bool m_gizmoActive = false;                       // gizmo claims the LMB (over a handle/dragged) → no rect-select
    bool m_toolCapture = false;                       // a viewport tool drives the LMB → no rect-select
    PandaUI::Point m_pressPosition;                   // fixed corner of the rectangle (screen points)
    // The navigation and the pointer since the last take.
    bool m_heldForward = false, m_heldBackward = false, m_heldLeft = false, m_heldRight = false;
    bool m_shiftHeld = false, m_controlHeld = false;
    bool m_primaryHeld = false; // ⌘ on macOS, Ctrl elsewhere: the steps of the gizmo (ScenePointer::snap)
    bool m_rightDown = false; // a right-button drag rotates the camera
    PandaUI::Point m_rotate;
    float m_zoom = 0.f;
    PandaUI::Point m_pointer;
    bool m_leftDown = false;
    bool m_leftPressed = false;
};

} // namespace Editor
