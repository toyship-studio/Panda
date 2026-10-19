//
// Created by Michael Andreichev on 29.11.2023.
//

#include "PandaUI/Runtime/Context.hpp"

#include "PandaUI/Core/Profile.hpp"
#include "PandaUI/Core/Theme.hpp"
#include "PandaUI/Elements/DragGhost.hpp"
#include "PandaUI/Elements/Tooltip.hpp"
#include "PandaUI/Rendering/UIRenderer.hpp"
#include "PandaUI/Runtime/ModalManager.hpp"

#include <algorithm>
#include <cassert>
#include <utility>

namespace PandaUI {

namespace {

    constexpr double TOOLTIP_DELAY = 0.5; // seconds of hovering before the tooltip is shown
    constexpr float DRAG_THRESHOLD = 5.f; // movement (px) from the press to the start of a drag

    bool isViewOrDescendantOf(View *target, View *root) {
        if (!target || !root) { return false; }
        for (View *view = target; view; view = view->getSuperview()) {
            if (view == root) { return true; }
        }
        return false;
    }

    Cursor resolveCursor(View *target) {
        for (View *view = target; view; view = view->getSuperview()) {
            if (view->getCursor() != Cursor::Inherit) { return view->getCursor(); }
        }
        return Cursor::Arrow;
    }

} // namespace

Context *Context::s_current = nullptr;
std::vector<Context *> Context::s_contexts;

Context &Context::current() {
    assert(s_current != nullptr && "UICONTEXT IS NOT CURRENTLY RENDERING");
    return *s_current;
}

Context::Context()
    : m_rootView(nullptr)
    , m_renderer(std::make_unique<UIRenderer>())
    , m_modals(std::make_unique<ModalManager>(*this))
    , m_viewportSize()
    , m_dpiScale(1.f, 1.f)
    , m_safeAreaInsets()
    , m_hoveredView(nullptr)
    , m_mouseCaptureView(nullptr)
    , m_focusedView(nullptr)
    , m_mousePosition() {
    // The theme change is hooked up to refreshing all contexts once. It is installed lazily (at runtime,
    // not in static-init) so as not to depend on the static initialization order across TUs.
    static const bool themeHandlerInstalled = [] {
        setThemeChangedHandler([] {
            for (Context *context : s_contexts) {
                if (context->m_rootView) { context->m_rootView->propagateThemeChanged(); }
                context->setNeedsRedraw();
            }
        });
        return true;
    }();
    (void)themeHandlerInstalled;

    m_camera.setViewportSize(m_viewportSize);
    m_renderer->setViewportSize(m_viewportSize, m_dpiScale);
    m_renderer->setViewProj(m_camera.getProjection());
    s_contexts.push_back(this);
    if (m_rootView) { m_rootView->setBackgroundColor(Color(0x00000000)); }
}

Context::~Context() {
    if (m_rootView) { m_rootView->setContext(nullptr); }
    for (const auto &overlay : m_overlays) { overlay->setContext(nullptr); }
    if (m_tooltipView) { m_tooltipView->setContext(nullptr); }
    if (m_dragGhost) { m_dragGhost->setContext(nullptr); }
    m_overlays.clear();
    s_contexts.erase(std::remove(s_contexts.begin(), s_contexts.end(), this), s_contexts.end());
    if (s_current == this) { s_current = nullptr; }
}

void Context::setFontAtlas(std::shared_ptr<FontAtlas> atlas, FontWeight weight, FontFamily family) {
    m_fonts[static_cast<int>(family)][static_cast<int>(weight)] = std::move(atlas);
}

std::shared_ptr<FontAtlas> Context::getFontAtlas(FontWeight weight, FontFamily family) {
    const int f = static_cast<int>(family);
    const int w = static_cast<int>(weight);
    if (m_fonts[f][w]) { return m_fonts[f][w]; }
    // Fallback: the same family Regular, then the base Sans Regular.
    const int regular = static_cast<int>(FontWeight::Regular);
    if (m_fonts[f][regular]) { return m_fonts[f][regular]; }
    return m_fonts[static_cast<int>(FontFamily::Sans)][regular];
}

bool Context::update(double deltaTime) {
    PANDA_UI_PROFILE_SCOPE_N("PandaUI::Context::update");
    if (!m_rootView) { return false; }

    Context *previous = s_current;
    s_current = this;
    m_clock += deltaTime; // monotonic clock (for double-click detection)

    // Overlays (popup/menu) with a deferred removal are removed at a safe point — before update/render.
    flushOverlayRemovals();

    // Deferred tasks (postDeferred): swap before the call — a task may post new ones.
    if (!m_deferredTasks.empty()) {
        std::vector<std::function<void()>> tasks;
        tasks.swap(m_deferredTasks);
        for (auto &task : tasks) { task(); }
    }

    // Animations, caret blinking and scroll physics step every tick and mark the
    // context dirty only when they actually change something.
    m_rootView->updateHierarchy(deltaTime);
    for (const auto &overlay : m_overlays) { overlay->updateHierarchy(deltaTime); }

    // Hover tooltip: accumulates time and shows/hides the bubble (marks redraw by itself).
    updateTooltip(deltaTime);

    // A layout change also arms redraw (setNeedsLayout calls setNeedsDisplay),
    // but the needsLayout check is duplicated so that a missed display flag never
    // cancels a required relayout.
    bool needsLayout = m_rootView->needsLayout();
    for (const auto &overlay : m_overlays) { needsLayout = needsLayout || overlay->needsLayout(); }
    if (m_tooltipView) { needsLayout = needsLayout || m_tooltipView->needsLayout(); }
    if (m_dragGhost) { needsLayout = needsLayout || m_dragGhost->needsLayout(); }
    const bool shouldRedraw = m_needsRedraw || needsLayout;
    // Reset before layout/render: if they dirty something again (for example,
    // ScrollView finishes computing the content size in viewDidCalculateLayout), it survives until
    // the next frame instead of being lost.
    m_needsRedraw = false;
    layoutIfNeeded();

    // The content may have moved under a still cursor (scroll/virtualization/list expansion) —
    // recompute the hover from the last mouse position after the layout, otherwise the hover "sticks"
    // to the row that has left, and the one that has arrived is not highlighted. Only when the layout
    // has actually changed.
    if (needsLayout && !m_mouseCaptureView && !m_dragActive) {
        PointerEvent hoverEvent;
        hoverEvent.type = PointerType::Mouse;
        hoverEvent.position = m_mousePosition;
        updateHoveredView(m_mousePosition, hoverEvent);
    }

    if (shouldRedraw) {
        m_renderer->begin();
        {
            PANDA_UI_PROFILE_SCOPE_N("PandaUI::View::render root");
            m_rootView->render(0, 0);
            for (const auto &overlay : m_overlays) { overlay->render(0, 0); }
            if (m_tooltipView) { m_tooltipView->render(0, 0); } // the tooltip — on top of everything
            if (m_dragGhost) { m_dragGhost->render(0, 0); }     // the drag ghost — the very top
        }
        m_renderer->end();
    }

    s_current = previous;
    return shouldRedraw;
}

void Context::layoutIfNeeded() {
    const bool rootNeeds = m_rootView && m_rootView->needsLayout();
    bool overlayNeeds = false;
    for (const auto &overlay : m_overlays) {
        if (overlay->needsLayout()) {
            overlayNeeds = true;
            break;
        }
    }
    const bool tooltipNeeds = m_tooltipView && m_tooltipView->needsLayout();
    const bool ghostNeeds = m_dragGhost && m_dragGhost->needsLayout();
    if (!rootNeeds && !overlayNeeds && !tooltipNeeds && !ghostNeeds) { return; }
    ++m_layoutPassCount;
    PANDA_UI_PROFILE_SCOPE_N("PandaUI::Context::layoutIfNeeded");
    Context *previous = s_current;
    s_current = this;
    if (rootNeeds) { m_rootView->calculateLayout(); }
    for (const auto &overlay : m_overlays) {
        if (overlay->needsLayout()) { overlay->calculateLayout(); }
    }
    if (tooltipNeeds) { m_tooltipView->calculateLayout(); }
    if (ghostNeeds) { m_dragGhost->calculateLayout(); }
    s_current = previous;
}

void Context::updateViewportSize(Size size, Size dpiScale) {
    m_viewportSize = size;
    m_dpiScale = dpiScale;
    m_camera.setViewportSize(size);
    m_renderer->setViewportSize(size, dpiScale);
    m_renderer->setViewProj(m_camera.getProjection());

    if (m_rootView) { m_rootView->layoutSetSize(size); }
    for (const auto &overlay : m_overlays) { overlay->layoutSetSize(size); }
    if (m_tooltipView) { m_tooltipView->layoutSetSize(size); }
    if (m_dragGhost) { m_dragGhost->layoutSetSize(size); }
    layoutIfNeeded();
}

void Context::setSafeAreaInsets(EdgeInsets insets) {
    if (m_safeAreaInsets == insets) { return; }
    m_safeAreaInsets = insets;
    if (m_rootView) { m_rootView->setNeedsLayout(); }
}

UIRenderer &Context::getRenderer() {
    return *m_renderer;
}

const DrawList &Context::getDrawList() const {
    return m_renderer->getDrawList();
}

Cursor Context::getCurrentCursor() const {
    return resolveCursor(m_mouseCaptureView ? m_mouseCaptureView : m_hoveredView);
}

bool Context::handlePointerDown(PointerEvent event) {
    if (!m_rootView) { return false; }
    hideTooltip(); // a click hides the tooltip
    layoutIfNeeded();

    if (event.type == PointerType::Mouse) {
        m_mousePosition = event.position;
        m_pointerDownPosition = event.position;
        // Double-click: the same spot (±5px) within the time threshold (0.35s).
        const float dx = event.position.x - m_lastClickPosition.x;
        const float dy = event.position.y - m_lastClickPosition.y;
        const bool nearLast = dx >= -5.f && dx <= 5.f && dy >= -5.f && dy <= 5.f;
        m_clickCount = (m_clock - m_lastClickTime <= 0.35 && nearLast) ? m_clickCount + 1 : 1;
        m_lastClickTime = m_clock;
        m_lastClickPosition = event.position;
        event.clickCount = m_clickCount;
        View *target = hitTest(m_mousePosition);
        setFocusedView(focusTargetFor(target));
        dispatchPointerDown(target, event);
        m_mouseCaptureView = event.isHandled ? target : nullptr;
        return event.isHandled;
    }

    event.isPrimary = m_touchPointers.empty();
    View *target = hitTest(event.position);
    setFocusedView(focusTargetFor(target));
    dispatchPointerDown(target, event);
    m_touchPointers[event.pointerId] = {event.isHandled ? target : nullptr, event.position};
    return event.isHandled;
}

bool Context::handlePointerUp(PointerEvent event) {
    if (!m_rootView) { return false; }
    layoutIfNeeded();

    if (event.type == PointerType::Mouse) {
        m_mousePosition = event.position;
        if (m_dragActive) {
            finishDrag(event.position, true);
            m_mouseCaptureView = nullptr;
            updateHoveredView(m_mousePosition, event);
            return true;
        }
        View *target = m_mouseCaptureView ? m_mouseCaptureView : hitTest(m_mousePosition);
        dispatchPointerUp(target, event);
        m_mouseCaptureView = nullptr;
        updateHoveredView(m_mousePosition, event);
        return event.isHandled;
    }

    auto it = m_touchPointers.find(event.pointerId);
    if (it == m_touchPointers.end()) { return false; }
    event.position = it->second.position;
    event.isPrimary = m_touchPointers.begin()->first == event.pointerId;
    dispatchPointerUp(it->second.target, event);
    m_touchPointers.erase(it);
    return event.isHandled;
}

bool Context::handlePointerMove(PointerEvent event) {
    if (!m_rootView) { return false; }
    layoutIfNeeded();

    if (event.type == PointerType::Mouse) {
        m_mousePosition = event.position;

        if (m_dragActive) {
            updateDrag(event.position);
            return true;
        }
        // A captured view + movement beyond the threshold → try to start a drag.
        if (m_mouseCaptureView) {
            const float dx = event.position.x - m_pointerDownPosition.x;
            const float dy = event.position.y - m_pointerDownPosition.y;
            if (dx * dx + dy * dy >= DRAG_THRESHOLD * DRAG_THRESHOLD) {
                beginDrag(event.position);
                if (m_dragActive) {
                    updateDrag(event.position);
                    return true;
                }
            }
        }

        if (!m_mouseCaptureView) { updateHoveredView(m_mousePosition, event); }
        View *target = m_mouseCaptureView ? m_mouseCaptureView : hitTest(m_mousePosition);
        dispatchPointerMove(target, event);
        return event.isHandled;
    }

    auto it = m_touchPointers.find(event.pointerId);
    View *target = nullptr;
    if (it != m_touchPointers.end()) {
        event.delta = Point(event.position.x - it->second.position.x, event.position.y - it->second.position.y);
        it->second.position = event.position;
        target = it->second.target ? it->second.target : hitTest(event.position);
    } else {
        target = hitTest(event.position);
        m_touchPointers[event.pointerId] = {target, event.position};
    }

    event.isPrimary = m_touchPointers.begin()->first == event.pointerId;
    dispatchPointerMove(target, event);
    return event.isHandled;
}

bool Context::handleScroll(ScrollEvent event) {
    if (!m_rootView) { return false; }
    hideTooltip(); // scrolling hides the tooltip
    layoutIfNeeded();
    dispatchScroll(hitTest(event.position), event);
    return event.isHandled;
}

bool Context::handleKeyDown(KeyEvent event) {
    if (!m_rootView) { return false; }

    if (m_dragActive && event.key == Key::Escape) {
        cancelDrag();
        m_mouseCaptureView = nullptr;
        return true;
    }

    if (event.key == Key::Tab) {
        event.isHandled = event.isShiftDown ? focusPreviousView() : focusNextView();
        return event.isHandled;
    }

    if (m_focusedView && !m_focusedView->canBecomeFocused()) { setFocusedView(nullptr); }
    dispatchKeyDown(m_focusedView, event);
    // Not consumed by the focus chain (or there is no focus) → hand over to the accelerators / hotkey manager.
    // BUT not during text input: TextField takes characters through char events and does NOT mark KeyDown as
    // handled, so single-key hotkeys (e.g. Z) would otherwise fire right while typing in the field.
    // And not while a popup is open: a menu/popover is modal for the keyboard — hotkeys
    // must not fire "through" an open menu.
    const bool textInputFocused = m_focusedView && m_focusedView->isTextInput();
    if (!event.isHandled && m_unhandledKeyDownHandler && !textInputFocused && m_overlays.empty()) {
        event.isHandled = m_unhandledKeyDownHandler(event);
    }
    return event.isHandled;
}

void Context::setUnhandledKeyDownHandler(std::function<bool(const KeyEvent &)> handler) {
    m_unhandledKeyDownHandler = std::move(handler);
}

bool Context::handleKeyUp(KeyEvent event) {
    if (!m_rootView) { return false; }
    if (m_focusedView && !m_focusedView->canBecomeFocused()) { setFocusedView(nullptr); }
    dispatchKeyUp(m_focusedView, event);
    return event.isHandled;
}

bool Context::handleTextInput(TextInputEvent event) {
    if (!m_rootView) { return false; }
    if (m_focusedView && !m_focusedView->canBecomeFocused()) { setFocusedView(nullptr); }
    dispatchTextInput(m_focusedView, event);
    return event.isHandled;
}

void Context::setFocusedView(View *view) {
    if (view && !view->canBecomeFocused()) { view = nullptr; }
    if (m_focusedView == view) { return; }

    if (m_focusedView) { m_focusedView->setFocused(false); }
    m_focusedView = view;
    if (m_focusedView) { m_focusedView->setFocused(true); }
    notifyFocusScopes(view); // popups close if the focus has moved outside them
}

void Context::addFocusScope(View *overlay, View *anchor, std::function<void()> onFocusOut) {
    m_focusScopes.push_back({overlay, anchor, std::move(onFocusOut)});
}

void Context::removeFocusScope(View *overlay) {
    m_focusScopes.erase(
        std::remove_if(m_focusScopes.begin(), m_focusScopes.end(), [overlay](const FocusScope &scope) {
            return scope.overlay == overlay;
        }),
        m_focusScopes.end()
    );
}

void Context::notifyFocusScopes(View *focus) {
    if (m_focusScopes.empty()) { return; }
    // A copy: onFocusOut usually closes the popup and calls removeFocusScope (which modifies the list).
    const std::vector<FocusScope> scopes = m_focusScopes;
    for (const FocusScope &scope : scopes) {
        const bool inside = isViewOrDescendantOf(focus, scope.overlay) || isViewOrDescendantOf(focus, scope.anchor);
        if (!inside && scope.onFocusOut) { scope.onFocusOut(); }
    }
}

bool Context::focusNextView() {
    std::vector<View *> focusableViews;
    // While a popup is open — Tab is trapped in the top overlay (a menu/popover is modal for the keyboard).
    View *traversalRoot = m_overlays.empty() ? m_rootView.get() : m_overlays.back().get();
    collectFocusableViews(traversalRoot, focusableViews);
    if (!m_overlays.empty()) {
        // The Popup itself is a frame, not an interactive element: it is excluded from the Tab cycle. If there
        // is nothing to focus inside — Tab is simply swallowed (the focus does not escape under the popup).
        focusableViews.erase(
            std::remove(focusableViews.begin(), focusableViews.end(), traversalRoot), focusableViews.end()
        );
        if (focusableViews.empty()) { return true; }
    }
    if (focusableViews.empty()) {
        setFocusedView(nullptr);
        return false;
    }

    if (!m_focusedView) {
        setFocusedView(focusableViews.front());
        return true;
    }

    auto current = std::find(focusableViews.begin(), focusableViews.end(), m_focusedView);
    if (current == focusableViews.end() || ++current == focusableViews.end()) {
        setFocusedView(focusableViews.front());
    } else {
        setFocusedView(*current);
    }
    return true;
}

bool Context::focusPreviousView() {
    std::vector<View *> focusableViews;
    View *traversalRoot = m_overlays.empty() ? m_rootView.get() : m_overlays.back().get();
    collectFocusableViews(traversalRoot, focusableViews);
    if (!m_overlays.empty()) {
        focusableViews.erase(
            std::remove(focusableViews.begin(), focusableViews.end(), traversalRoot), focusableViews.end()
        );
        if (focusableViews.empty()) { return true; }
    }
    if (focusableViews.empty()) {
        setFocusedView(nullptr);
        return false;
    }

    if (!m_focusedView) {
        setFocusedView(focusableViews.back());
        return true;
    }

    auto current = std::find(focusableViews.begin(), focusableViews.end(), m_focusedView);
    if (current == focusableViews.end() || current == focusableViews.begin()) {
        setFocusedView(focusableViews.back());
    } else {
        setFocusedView(*(--current));
    }
    return true;
}

void Context::forgetView(View *view) {
    if (!view) { return; }

    cancelInteractionInSubtree(view);

    if (isViewOrDescendantOf(m_hoveredView, view)) {
        View *previousHoveredView = m_hoveredView;
        m_hoveredView = nullptr;
        if (previousHoveredView->m_isHovered) {
            PointerEvent event;
            event.type = PointerType::Mouse;
            event.position = m_mousePosition;
            previousHoveredView->m_isHovered = false;
            previousHoveredView->pointerLeave(event);
            previousHoveredView->hoverChanged(false);
        }
    }
    if (isViewOrDescendantOf(m_mouseCaptureView, view)) { m_mouseCaptureView = nullptr; }
    if (isViewOrDescendantOf(m_focusedView, view)) { setFocusedView(nullptr); }
    if (isViewOrDescendantOf(m_tooltipTargetView, view)) {
        m_tooltipTargetView = nullptr;
        hideTooltip();
    }
    if (m_dragActive) {
        // The source/target may be recreated (the retained tree rebuilds rows on
        // spring expansion). The drag is held by the payload, so it is NOT cancelled — only the
        // dangling pointers are nulled so that updateDrag/finishDrag do not touch a dead view.
        if (isViewOrDescendantOf(m_dragSource, view)) { m_dragSource = nullptr; }
        if (isViewOrDescendantOf(m_dropTarget, view)) { m_dropTarget = nullptr; }
    }
    for (auto &entry : m_touchPointers) {
        if (isViewOrDescendantOf(entry.second.target, view)) { entry.second.target = nullptr; }
    }
    for (OverlayFocusRestore &entry : m_overlayFocusRestore) {
        if (isViewOrDescendantOf(entry.previousFocus, view)) { entry.previousFocus = nullptr; }
    }
}

void Context::forgetViewInAllContexts(View *view) {
    for (Context *context : s_contexts) {
        if (context) { context->forgetView(view); }
    }
}

void Context::forgetFocusInAllContexts(View *view) {
    if (!view) { return; }
    for (Context *context : s_contexts) {
        if (context && context->m_focusedView == view) { context->setFocusedView(nullptr); }
    }
}

View *Context::hitTest(Point position) {
    layoutIfNeeded();
    // Overlays intercept events on top of root (the last one is the topmost).
    for (auto it = m_overlays.rbegin(); it != m_overlays.rend(); ++it) {
        if (View *hit = (*it)->hitTest(position)) { return hit; }
    }
    if (!m_rootView) { return nullptr; }
    return m_rootView->hitTest(position);
}

void Context::addOverlay(std::shared_ptr<View> overlay) {
    if (!overlay) { return; }
    // Before setContext: a popup menu focuses its content right from contextAttached.
    m_overlayFocusRestore.push_back({overlay.get(), m_focusedView});
    overlay->m_superview = nullptr;
    overlay->setContext(this);
    overlay->layoutSetSize(m_viewportSize);
    overlay->setNeedsLayout();
    m_overlays.push_back(std::move(overlay));
    setNeedsRedraw();
}

void Context::requestRemoveOverlay(View *overlay) {
    if (!overlay) { return; }
    if (std::find(m_overlaysToRemove.begin(), m_overlaysToRemove.end(), overlay) == m_overlaysToRemove.end()) {
        m_overlaysToRemove.push_back(overlay);
    }
    setNeedsRedraw();
}

std::vector<View *> Context::overlaysAbove(View *overlay) const {
    std::vector<View *> above;
    bool found = false;
    for (const auto &candidate : m_overlays) {
        if (found) { above.push_back(candidate.get()); }
        if (candidate.get() == overlay) { found = true; }
    }
    return above;
}

void Context::postDeferred(std::function<void()> task) {
    if (!task) { return; }
    m_deferredTasks.push_back(std::move(task));
}

void Context::flushOverlayRemovals() {
    if (m_overlaysToRemove.empty()) { return; }
    for (View *overlay : m_overlaysToRemove) {
        const bool focusWasInside = isViewOrDescendantOf(m_focusedView, overlay);
        forgetView(overlay);
        auto it = std::find_if(m_overlays.begin(), m_overlays.end(), [overlay](const std::shared_ptr<View> &candidate) {
            return candidate.get() == overlay;
        });
        if (it != m_overlays.end()) {
            (*it)->setContext(nullptr);
            (*it)->m_superview = nullptr;
            m_overlays.erase(it);
        }
        // Return the focus to the owner (the menu button / panel) unless the popup has handed it outside
        // by itself: a click elsewhere has already moved the focus — then it is left alone.
        auto restoreIt = std::find_if(
            m_overlayFocusRestore.begin(), m_overlayFocusRestore.end(),
            [overlay](const OverlayFocusRestore &entry) { return entry.overlay == overlay; }
        );
        if (restoreIt != m_overlayFocusRestore.end()) {
            View *previousFocus = restoreIt->previousFocus;
            m_overlayFocusRestore.erase(restoreIt);
            if ((focusWasInside || !m_focusedView) && previousFocus) { setFocusedView(previousFocus); }
        }
    }
    m_overlaysToRemove.clear();
}

void Context::removeAllOverlays() {
    for (const auto &overlay : m_overlays) {
        forgetView(overlay.get());
        overlay->setContext(nullptr);
    }
    m_overlays.clear();
    m_overlaysToRemove.clear();
    m_overlayFocusRestore.clear();
    m_focusScopes.clear();
    m_modals->clear();
}

View *Context::resolveTooltipTarget(View *view) const {
    for (View *current = view; current; current = current->getSuperview()) {
        if (!current->getTooltip().empty()) { return current; }
    }
    return nullptr;
}

void Context::updateTooltip(double deltaTime) {
    // The target is the nearest ancestor of the hovered view with a non-empty tooltip. During a drag
    // (mouse capture) tooltips are not shown.
    View *target = m_mouseCaptureView ? nullptr : resolveTooltipTarget(m_hoveredView);

    if (target != m_tooltipTargetView) {
        m_tooltipTargetView = target;
        m_tooltipElapsed = 0.0;
        hideTooltip();
        return;
    }
    if (!target || m_tooltipView) { return; } // no target or the bubble is already shown

    m_tooltipElapsed += deltaTime;
    if (m_tooltipElapsed < TOOLTIP_DELAY) { return; }

    m_tooltipView = std::make_shared<Tooltip>(target->getTooltip(), target->getWorldFrame());
    m_tooltipView->setContext(this);
    m_tooltipView->layoutSetSize(m_viewportSize);
    m_tooltipView->setNeedsLayout();
    setNeedsRedraw();
}

void Context::hideTooltip() {
    m_tooltipElapsed = 0.0;
    if (!m_tooltipView) { return; }
    m_tooltipView->setContext(nullptr);
    m_tooltipView.reset();
    setNeedsRedraw();
}

DragInfo Context::makeDragInfo(View *target, Point windowPosition) const {
    DragInfo info;
    info.payload = m_dragPayload;
    info.windowPosition = windowPosition;
    if (target) {
        const Rect frame = target->getWorldFrame();
        info.localPosition = Point(windowPosition.x - frame.origin.x, windowPosition.y - frame.origin.y);
    }
    return info;
}

View *Context::findDropTarget(View *hit) const {
    for (View *view = hit; view; view = view->getSuperview()) {
        if (view->canAcceptDrop(makeDragInfo(view, m_mousePosition))) { return view; }
    }
    return nullptr;
}

void Context::beginDrag(Point position) {
    // The source is the first ancestor of the captured view (including the view itself) whose prepareDrag
    // yields a payload.
    for (View *view = m_mouseCaptureView; view; view = view->getSuperview()) {
        DragPayload payload;
        const Rect frame = view->getWorldFrame();
        const Point local(m_pointerDownPosition.x - frame.origin.x, m_pointerDownPosition.y - frame.origin.y);
        if (!view->prepareDrag(payload, local)) { continue; }

        m_dragActive = true;
        m_dragSource = view;
        m_dragPayload = std::move(payload);
        m_dropTarget = nullptr;
        // The "pressed" state of the captured control is cleared — there will be no click/action.
        if (m_mouseCaptureView) { m_mouseCaptureView->interactionCancelled(); }

        if (!m_dragPayload.ghostText.empty()) {
            m_dragGhost = std::make_shared<DragGhost>(m_dragPayload.ghostText);
            m_dragGhost->setContext(this);
            m_dragGhost->layoutSetSize(m_viewportSize);
            m_dragGhost->setCursorPosition(position);
            m_dragGhost->setNeedsLayout();
        }
        setNeedsRedraw();
        return;
    }
}

void Context::updateDrag(Point position) {
    m_mousePosition = position;
    if (m_dragGhost) { m_dragGhost->setCursorPosition(position); }

    View *target = findDropTarget(hitTest(position));
    if (target != m_dropTarget) {
        if (m_dropTarget) { m_dropTarget->dragExited(); }
        m_dropTarget = target;
        if (m_dropTarget) { m_dropTarget->dragEntered(makeDragInfo(m_dropTarget, position)); }
    }
    if (m_dropTarget) { m_dropTarget->dragUpdated(makeDragInfo(m_dropTarget, position)); }
    setNeedsRedraw();
}

void Context::finishDrag(Point position, bool drop) {
    // The state is reset BEFORE the callbacks: performDrop may delete views, and ~View/removeSubview
    // call forgetView → cancelDrag. Without the early reset this would re-enter finishDrag.
    View *target = m_dropTarget;
    const DragInfo info = makeDragInfo(target, position);
    m_dropTarget = nullptr;
    m_dragSource = nullptr;
    m_dragActive = false;
    m_dragPayload = {};
    if (m_dragGhost) {
        m_dragGhost->setContext(nullptr);
        m_dragGhost.reset();
    }

    if (target && drop) { target->performDrop(info); }
    if (target) { target->dragExited(); }
    setNeedsRedraw();
}

void Context::cancelDrag() {
    if (!m_dragActive) { return; }
    finishDrag(m_mousePosition, false);
}

View *Context::focusTargetFor(View *target) const {
    for (View *view = target; view; view = view->getSuperview()) {
        if (view->canBecomeFocused()) { return view; }
    }
    return nullptr;
}

void Context::collectFocusableViews(View *view, std::vector<View *> &views) const {
    if (!view || view->isHidden() || !view->isEnabled() || !view->isUserInteractionEnabled() ||
        view->getOpacity() <= 0.f) {
        return;
    }

    if (view->canBecomeFocused()) { views.push_back(view); }
    for (const std::shared_ptr<View> &subview : view->getSubviews()) {
        collectFocusableViews(subview.get(), views);
    }
}

void Context::cancelInteractionInSubtree(View *view) {
    if (!view) { return; }
    view->interactionCancelled();
    for (const std::shared_ptr<View> &subview : view->getSubviews()) {
        cancelInteractionInSubtree(subview.get());
    }
}

void Context::updateHoveredView(Point position, PointerEvent &event) {
    View *newHoveredView = hitTest(position);
    if (newHoveredView == m_hoveredView) { return; }

    if (m_hoveredView) {
        m_hoveredView->m_isHovered = false;
        m_hoveredView->pointerLeave(event);
        m_hoveredView->hoverChanged(false);
    }
    m_hoveredView = newHoveredView;
    if (m_hoveredView) {
        m_hoveredView->m_isHovered = true;
        m_hoveredView->pointerEnter(event);
        m_hoveredView->hoverChanged(true);
    }
}

void Context::dispatchPointerDown(View *target, PointerEvent &event) {
    for (View *view = target; view && !event.isHandled; view = view->getSuperview()) {
        if (view->pointerDown(event)) { event.isHandled = true; }
    }
}

void Context::dispatchPointerUp(View *target, PointerEvent &event) {
    for (View *view = target; view && !event.isHandled; view = view->getSuperview()) {
        if (view->pointerUp(event)) { event.isHandled = true; }
    }
}

void Context::dispatchPointerMove(View *target, PointerEvent &event) {
    for (View *view = target; view && !event.isHandled; view = view->getSuperview()) {
        if (view->pointerMove(event)) { event.isHandled = true; }
    }
}

void Context::dispatchScroll(View *target, ScrollEvent &event) {
    for (View *view = target; view && !event.isHandled; view = view->getSuperview()) {
        if (view->scroll(event)) { event.isHandled = true; }
    }
}

void Context::dispatchKeyDown(View *target, KeyEvent &event) {
    for (View *view = target; view && !event.isHandled; view = view->getSuperview()) {
        if (view->keyDown(event)) { event.isHandled = true; }
    }
}

void Context::dispatchKeyUp(View *target, KeyEvent &event) {
    for (View *view = target; view && !event.isHandled; view = view->getSuperview()) {
        if (view->keyUp(event)) { event.isHandled = true; }
    }
}

void Context::dispatchTextInput(View *target, TextInputEvent &event) {
    for (View *view = target; view && !event.isHandled; view = view->getSuperview()) {
        if (view->textInput(event)) { event.isHandled = true; }
    }
}

} // namespace PandaUI
