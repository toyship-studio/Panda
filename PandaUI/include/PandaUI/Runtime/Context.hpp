//
// Created by Michael Andreichev on 29.11.2023.
//

#pragma once

#include "PandaUI/Rendering/Font.hpp"
#include "PandaUI/Rendering/DrawList.hpp"
#include "PandaUI/Rendering/TextLayout.hpp" // FontWeight
#include "PandaUI/Rendering/UICamera.hpp"
#include "PandaUI/Core/View.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace PandaUI {

class UIRenderer;
class DragGhost;
class ModalManager;

class Context final {
public:
    Context();
    ~Context();

    /// Updates animations/layout and rebuilds the draw-list only if the UI has become
    /// "dirty". Returns true if a new frame was produced during this tick and the host must
    /// (re)submit the draw-list; false means that nothing has changed and the host
    /// may skip rendering this window — the previous frame stays on screen.
    bool update(double deltaTime);
    void updateViewportSize(Size size, Size dpiScale = Size(1.f, 1.f));
    void setSafeAreaInsets(EdgeInsets insets);
    void layoutIfNeeded();
    // Number of layout passes actually performed over the lifetime of the context: the per-frame delta
    // in the statistics catches "someone dirties the layout every frame" bugs.
    uint64_t layoutPassCount() const {
        return m_layoutPassCount;
    }
    /// Any view tells its context that the cached frame is stale.
    void setNeedsRedraw() {
        m_needsRedraw = true;
    }
    bool needsRedraw() const {
        return m_needsRedraw;
    }
    bool handlePointerDown(PointerEvent event);
    bool handlePointerUp(PointerEvent event);
    bool handlePointerMove(PointerEvent event);
    bool handleScroll(ScrollEvent event);
    bool handleKeyDown(KeyEvent event);
    bool handleKeyUp(KeyEvent event);
    bool handleTextInput(TextInputEvent event);

    // Global handler for keys not consumed by the focus chain (or when there is no focus) — the basis for
    // accelerators / a hotkey manager. Called after dispatchKeyDown; returned true → consumed.
    void setUnhandledKeyDownHandler(std::function<bool(const KeyEvent &)> handler);

    void setFocusedView(View *view);
    View *getFocusedView() const {
        return m_focusedView;
    }
    bool focusNextView();
    bool focusPreviousView();
    Size getViewportSize() {
        return m_viewportSize;
    }
    EdgeInsets getSafeAreaInsets() const {
        return m_safeAreaInsets;
    }
    Cursor getCurrentCursor() const;
    template<typename T, typename... Args>
    std::shared_ptr<T> makeView(Args &&...args) {
        return std::make_shared<T>(std::forward<Args>(args)...);
    }

    std::shared_ptr<View> getRootView() {
        return m_rootView;
    }

    // Overlay layer: drawn on top of root and hit-tested first (popup / menu / tooltip).
    // Removal is deferred — it is safe to call from an event handler of the overlay itself.
    void addOverlay(std::shared_ptr<View> overlay);
    void requestRemoveOverlay(View *overlay);
    // Overlays lying above the given one (bottom to top): a modal removes its popups when it closes.
    std::vector<View *> overlaysAbove(View *overlay) const;

    // Blocking modals (Modal/Dialog) are shown only through the manager: stack + ownership.
    ModalManager &modals() {
        return *m_modals;
    }

    // Runs a task at the safe point at the start of the next update tick (outside event dispatch).
    // For work that destroys widgets from their own callbacks (rebuilding a form on a value change, etc.).
    void postDeferred(std::function<void()> task);

    // Popup focus scope: while the focus is inside overlay or anchor — all is fine; as soon as the focus
    // moves outside them, onFocusOut is called (popups use it to close on focus loss).
    void addFocusScope(View *overlay, View *anchor, std::function<void()> onFocusOut);
    void removeFocusScope(View *overlay);

    void setRootView(std::shared_ptr<View> view) {
        setFocusedView(nullptr);
        removeAllOverlays();
        hideTooltip();
        m_tooltipTargetView = nullptr;
        cancelDrag();
        if (m_rootView) {
            forgetView(m_rootView.get());
            m_rootView->m_superview = nullptr;
            m_rootView->setContext(nullptr);
        }
        m_rootView = view;
        if (m_rootView) {
            m_rootView->m_superview = nullptr;
            m_rootView->setContext(this);
            m_rootView->layoutSetSize(m_viewportSize);
            m_rootView->setNeedsLayout();
        }
    }

    UIRenderer &getRenderer();
    const DrawList &getDrawList() const;
    // Single point for working with fonts: family × weight. Sans Regular is the base and the last
    // fallback. A missing pair falls back: the requested one → the same family Regular → Sans of the
    // weight → Sans Regular. Headings — Bold, small text — Medium, body — Regular; numbers/paths/IDs —
    // Mono. A family is a role or a name (fontFamily("Cinzel")); the host decides which face plays it.
    void setFontAtlas(std::shared_ptr<FontAtlas> atlas, FontWeight weight = FontWeight::Regular, FontFamily family = FontFamily::Sans);
    std::shared_ptr<FontAtlas> getFontAtlas(FontWeight weight = FontWeight::Regular, FontFamily family = FontFamily::Sans);
    // The scale of every text size: the UI font size setting over the 16 the UI is designed at.
    // Geometry is not scaled; controls with a minimum height grow with their text.
    void setTextScale(float scale);
    float textScale() const {
        return m_textScale;
    }
    // The atlases or the scale were replaced: every text measures and draws itself anew — the whole
    // tree of every context, like a theme change.
    void fontsChanged();

    static Context &current();
    static bool hasCurrent() {
        return s_current != nullptr;
    }
    static void forgetViewInAllContexts(View *view);

private:
    struct PointerState {
        View *target = nullptr;
        Point position;
    };

    // Registered popup focus scope (see addFocusScope).
    struct FocusScope {
        View *overlay = nullptr;
        View *anchor = nullptr;
        std::function<void()> onFocusOut;
    };

    // Focus at the moment the overlay is shown: restored when the overlay is removed, unless the popup
    // has handed the focus outside by itself (previousFocus is nulled in forgetView when the view dies).
    struct OverlayFocusRestore {
        View *overlay = nullptr;
        View *previousFocus = nullptr;
    };

    void forgetView(View *view);
    void notifyFocusScopes(View *focus); // close the popups whose focus scopes the focus has left
    void flushOverlayRemovals();
    void removeAllOverlays();
    // Hover tooltips: show timer, hiding, lookup of the nearest ancestor with a tooltip.
    void updateTooltip(double deltaTime);
    void hideTooltip();
    View *resolveTooltipTarget(View *view) const;
    // Drag and drop: start past a threshold, tracking of drop targets, completion/cancellation.
    void beginDrag(Point position);
    void updateDrag(Point position);
    void finishDrag(Point position, bool drop);
    void cancelDrag();
    View *findDropTarget(View *hit) const;
    DragInfo makeDragInfo(View *target, Point windowPosition) const;
    View *hitTest(Point position);
    View *focusTargetFor(View *target) const;
    void collectFocusableViews(View *view, std::vector<View *> &views) const;
    void cancelInteractionInSubtree(View *view);
    void updateHoveredView(Point position, PointerEvent &event);
    void dispatchPointerDown(View *target, PointerEvent &event);
    void dispatchPointerUp(View *target, PointerEvent &event);
    void dispatchPointerMove(View *target, PointerEvent &event);
    void dispatchScroll(View *target, ScrollEvent &event);
    void dispatchKeyDown(View *target, KeyEvent &event);
    void dispatchKeyUp(View *target, KeyEvent &event);
    void dispatchTextInput(View *target, TextInputEvent &event);

    static Context *s_current;
    static std::vector<Context *> s_contexts;
    static void forgetFocusInAllContexts(View *view);
    std::shared_ptr<View> m_rootView;
    std::unique_ptr<UIRenderer> m_renderer;
    std::unique_ptr<ModalManager> m_modals;
    UICamera m_camera;
    // Fonts by family id, each with its three weights (the FontWeight values index the array).
    static constexpr int kFontWeightCount = 3; // Regular, Medium, Bold
    std::unordered_map<uint32_t, std::array<std::shared_ptr<FontAtlas>, kFontWeightCount>> m_fonts;
    float m_textScale = 1.f;
    Size m_viewportSize;
    Size m_dpiScale;
    EdgeInsets m_safeAreaInsets;
    View *m_hoveredView;
    View *m_mouseCaptureView;
    View *m_focusedView;
    Point m_mousePosition;
    std::unordered_map<int, PointerState> m_touchPointers;
    bool m_needsRedraw = true;
    uint64_t m_layoutPassCount = 0;
    std::vector<std::shared_ptr<View>> m_overlays;
    std::vector<View *> m_overlaysToRemove;
    std::vector<OverlayFocusRestore> m_overlayFocusRestore; // focus before each overlay was shown
    std::vector<std::function<void()>> m_deferredTasks; // one-shot tick-start tasks (postDeferred)
    std::vector<FocusScope> m_focusScopes;                          // focus scopes of the open popups
    std::function<bool(const KeyEvent &)> m_unhandledKeyDownHandler; // accelerators / hotkeys
    // Tooltip: the hover target (with a non-empty tooltip), the accumulated hover time and
    // the shown bubble (separate from m_overlays, drawn on top and not hit-tested).
    View *m_tooltipTargetView = nullptr;
    double m_tooltipElapsed = 0.0;
    std::shared_ptr<View> m_tooltipView;
    // Drag and drop: the press position (for the start threshold), the active session, the payload,
    // the source and the current target, the "ghost" at the cursor.
    Point m_pointerDownPosition;
    double m_clock = 0.0;            // monotonic time (accumulated in update) — for double-click
    double m_lastClickTime = -100.0; // time of the previous mouse-down
    Point m_lastClickPosition;       // position of the previous mouse-down
    int m_clickCount = 0;            // number of consecutive clicks within the threshold (1, 2, …)
    bool m_dragActive = false;
    DragPayload m_dragPayload;
    View *m_dragSource = nullptr;
    View *m_dropTarget = nullptr;
    std::shared_ptr<DragGhost> m_dragGhost;

    friend class View;
};

} // namespace PandaUI
