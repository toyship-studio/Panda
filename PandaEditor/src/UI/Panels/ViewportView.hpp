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

// 3D scene surface. INTENTIONALLY narrow (see migration-readiness): shows the scene texture through
// ImageView (the engine's FBO is fed into setSceneTexture on migration — seam #1), catches focus,
// handles the gizmo switching keys (1/2/3, as in PandaEditor) and records the local coordinates of a
// click for mouse-picking. Does NOT implement camera navigation (trackpad rotation + WASD) — it stays
// in PandaEditor CameraController as is and is connected on migration.
class ViewportView final : public PandaUI::Panel {
public:
    explicit ViewportView(ViewportViewOutput *output);
    // Scene FBO texture (seam #1; empty before the migration). flipVertical=true for the engine's
    // render-to-texture (it arrives flipped along Y).
    void setSceneTexture(PandaUI::TextureHandle texture, bool flipVertical = false);
    // The gizmo is "over a handle"/being dragged → ViewportView does not consume the LMB (rect-select),
    // lets it through to the gizmo's Panda::Input.
    void setGizmoActive(bool active) {
        m_gizmoActive = active;
    }
    // PLAY: input in the viewport belongs to the GAME (clicks/keys go to Panda::Input, scripts read
    // them). Alt+LMB remains the editor pick (rect-select). Set by EditorLayer every frame from the
    // scene mode.
    void setGameInputMode(bool gameInput) {
        m_gameInput = gameInput;
    }
    // A viewport tool (terrain brush etc., EditorExtensions::viewportTools) drives the LMB → it goes
    // to the tool (Panda::Input), rect-select does not start. Set by EditorLayer every frame.
    void setToolCapture(bool capture) {
        m_toolCapture = capture;
    }

protected:
    bool pointerDown(PandaUI::PointerEvent &event) override; // focus + start of rect-select (LMB)
    bool pointerMove(PandaUI::PointerEvent &event) override; // dragging the rectangle → selection update
    bool pointerUp(PandaUI::PointerEvent &event) override;   // release → rect-select commit
    bool keyDown(PandaUI::KeyEvent &event) override;         // 1/2/3 — gizmo mode; the rest is passed through

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
    bool m_gameInput = false;                         // PLAY: input goes to the game (Alt+LMB — editor pick)
    bool m_toolCapture = false;                       // a viewport tool drives the LMB → no rect-select
    PandaUI::Point m_pressPosition;                   // fixed corner of the rectangle (screen points)
};

} // namespace Editor
