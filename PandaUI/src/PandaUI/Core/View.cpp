#include "PandaUI/Core/View.hpp"
#include "PandaUI/Core/Theme.hpp"
#include "PandaUI/Runtime/Context.hpp"
#include "PandaUI/Rendering/UIRenderer.hpp"
#include "PandaUI/Core/ViewInternal.hpp"

#include <yoga/Yoga.h>

#include <algorithm>
#include <cassert>
#include <memory>

namespace PandaUI {

namespace {

    float resolveMeasuredSize(float measured, float available, YGMeasureMode mode) {
        switch (mode) {
            case YGMeasureModeExactly:
                return available;
            case YGMeasureModeAtMost:
                return std::min(measured, available);
            case YGMeasureModeUndefined:
                return measured;
        }
        return measured;
    }

    YGSize
    measureView(YGNodeConstRef node, float width, YGMeasureMode widthMode, float height, YGMeasureMode heightMode) {
        auto *view = static_cast<View *>(YGNodeGetContext(node));
        if (!view) { return {0.f, 0.f}; }

        Size availableSize;
        availableSize.width = widthMode == YGMeasureModeUndefined ? 0.f : width;
        availableSize.height = heightMode == YGMeasureModeUndefined ? 0.f : height;

        const Size measuredSize = view->intrinsicContentSize(availableSize);
        return {
            resolveMeasuredSize(measuredSize.width, width, widthMode),
            resolveMeasuredSize(measuredSize.height, height, heightMode),
        };
    }

} // namespace

View::View()
    : View(Rect::zero()) {}

View::View(Rect frame)
    : m_frame(frame)
    , m_frameOffset()
    , m_layout(this)
    , m_surface(this)
    , m_internal(std::make_unique<ViewInternal>())
    , m_superview(nullptr)
    , m_context(nullptr)
    , m_transform(1.f)
    , m_needsLayout(true)
    , m_isHidden(false)
    , m_isEnabled(true)
    , m_isFocusable(false)
    , m_isFocused(false)
    , m_isUserInteractionEnabled(true)
    , m_isHovered(false)
    , m_cursor(Cursor::Inherit)
    , m_themeGeneration(themeGeneration()) { // built against the current theme
    m_internal->ref = YGNodeNew();
    configureYogaNode();
    if (frame.size.width != 0.f || frame.size.height != 0.f) { layoutSetSize(frame.size); }
    if (frame.origin.x != 0.f || frame.origin.y != 0.f) { layoutSetOrigin(frame.origin); }
}

View::~View() {
    Context::forgetViewInAllContexts(this);
    if (m_superview) { m_superview->removeSubview(this); }
    removeAllSubviews();
    if (m_internal && m_internal->ref) {
        YGNodeFree(m_internal->ref);
        m_internal->ref = nullptr;
    }
}

void View::notifyWillCalculateLayout() {
    viewWillCalculateLayout();
    for (const auto &view : m_subviews) {
        view->notifyWillCalculateLayout();
    }
}

void View::updateHierarchy(double deltaTime) {
    if (m_isHidden) { return; }
    update(deltaTime);
    // By index, holding the child: an update() may add or remove siblings, or remove itself.
    for (size_t i = 0; i < m_subviews.size(); ++i) {
        const std::shared_ptr<View> view = m_subviews[i];
        view->updateHierarchy(deltaTime);
    }
}

void View::layoutSetSize(Size size) {
    m_layout.setSize(size);
}

void View::layoutSetSizePercent(Size size) {
    m_layout.setWidth(Length::percent(size.width));
    m_layout.setHeight(Length::percent(size.height));
}

void View::layoutSetMargins(EdgeInsets margins) {
    m_layout.setMargin(Edge::Top, margins.top);
    m_layout.setMargin(Edge::Right, margins.right);
    m_layout.setMargin(Edge::Bottom, margins.bottom);
    m_layout.setMargin(Edge::Left, margins.left);
}

void View::layoutSetOrigin(Point point) {
    m_layout.setPosition(Edge::Left, Length::points(point.x));
    m_layout.setPosition(Edge::Top, Length::points(point.y));
}

void View::layoutSetOriginPercent(Point point) {
    m_layout.setPosition(Edge::Left, Length::percent(point.x));
    m_layout.setPosition(Edge::Top, Length::percent(point.y));
}

void View::layoutReset() {
    m_layout.reset();
}

void View::layoutSetRelative() {
    m_layout.setPositionType(PositionType::Relative);
}

void View::layoutSetAbsolute() {
    m_layout.setPositionType(PositionType::Absolute);
}

void View::layoutSetInsetsFromParent(EdgeInsets insets) {
    m_layout.setInsets(insets);
}

void View::addSubview(std::shared_ptr<View> view) {
    assert(view != nullptr && "INVALID NODE");
    assert(view.get() != this && "VIEW CANNOT BE ADDED TO ITSELF");
    // A measure-leaf cannot have children: a view with an intrinsic content size (Label/TextField, etc.)
    // installs a Yoga measure function, and Yoga forbids children on such nodes. Without this assert the
    // result is an obscure runtime crash std::logic_error("Nodes with measure functions cannot have children")
    // when the child is inserted below (YGNodeInsertChild). Icons/accessories/chips go into a wrapper
    // container, not into the field.
    assert(
        !(m_internal && m_internal->usesIntrinsicContentSize) &&
        "VIEW WITH INTRINSIC CONTENT SIZE (measure-leaf: Label/TextField) CANNOT HAVE CHILDREN — wrap it in a container view"
    );
    if (view->m_superview) { view->removeFromSuperview(); }
    m_subviews.push_back(view);
    view->m_superview = this;
    view->setContext(m_context);
    YGNodeRef thisYogaView = m_internal->ref;
    YGNodeRef thatYogaView = view->m_internal->ref;
    int subviewsCount = YGNodeGetChildCount(thisYogaView);
    YGNodeInsertChild(thisYogaView, thatYogaView, subviewsCount);
    setNeedsLayout();
}

void View::setContext(Context *context) {
    if (m_context == context) { return; }
    m_context = context;
    // The theme changed while this view was out of the tree (a hidden root, a replaced tab content):
    // it catches up now, before anyone sees it. Each subview checks itself in the recursion.
    if (m_context && m_themeGeneration != themeGeneration()) { restyle(); }
    for (const auto &subview : m_subviews) {
        if (subview) { subview->setContext(context); }
    }
    if (m_context) { contextAttached(); }
}

void View::restyle() {
    m_themeGeneration = themeGeneration();
    themeChanged();
}

void View::resolveHierarchy() {
    YGNodeRef thisYogaView = m_internal->ref;
    YGNodeRemoveAllChildren(thisYogaView);
    for (const auto &view : m_subviews) {
        YGNodeRef thatYogaView = view->m_internal->ref;
        int subviewsCount = YGNodeGetChildCount(thisYogaView);
        YGNodeInsertChild(thisYogaView, thatYogaView, subviewsCount);
        view->m_superview = this;
        view->setContext(m_context);
        view->resolveHierarchy();
    }
}

void View::removeSubview(std::shared_ptr<View> node) {
    assert(node != nullptr && "INVALID NODE");
    removeSubview(node.get());
}

void View::removeSubview(View *view) {
    assert(view != nullptr && "INVALID NODE");
    auto it = std::find_if(m_subviews.begin(), m_subviews.end(), [view](const std::shared_ptr<View> &subview) {
        return subview.get() == view;
    });
    if (it == m_subviews.end()) { return; }

    Context::forgetViewInAllContexts(view);
    YGNodeRemoveChild(m_internal->ref, view->m_internal->ref);
    view->m_superview = nullptr;
    view->setContext(nullptr);
    m_subviews.erase(it);
    setNeedsLayout();
}

void View::removeAllSubviews() {
    if (!m_internal || !m_internal->ref) {
        for (auto &subview : m_subviews) {
            if (!subview) { continue; }
            subview->m_superview = nullptr;
            subview->setContext(nullptr);
        }
        m_subviews.clear();
        return;
    }

    for (auto &subview : m_subviews) {
        if (subview) { Context::forgetViewInAllContexts(subview.get()); }
        if (subview && subview->m_internal && subview->m_internal->ref) {
            YGNodeRemoveChild(m_internal->ref, subview->m_internal->ref);
        }
        if (subview) {
            subview->m_superview = nullptr;
            subview->setContext(nullptr);
        }
    }
    m_subviews.clear();
    setNeedsLayout();
}

void View::removeFromSuperview() {
    if (!m_superview) { return; }
    m_superview->removeSubview(this);
}

void View::calculateLayout() {
    notifyWillCalculateLayout();
    YGNodeCalculateLayout(m_internal->ref, YGUndefined, YGUndefined, YGDirectionLTR);
    applyCalculatedLayout();
}

void View::applyCalculatedLayout() {
    YGNodeRef thisYogaView = m_internal->ref;
    m_frame.origin.x = YGNodeLayoutGetLeft(thisYogaView);
    m_frame.origin.y = YGNodeLayoutGetTop(thisYogaView);
    m_frame.size.width = YGNodeLayoutGetWidth(thisYogaView);
    m_frame.size.height = YGNodeLayoutGetHeight(thisYogaView);
    m_needsLayout = false;
    for (const auto &subview : m_subviews) {
        subview->applyCalculatedLayout();
    }
    viewDidCalculateLayout();
}

void View::setNeedsLayout() {
    m_needsLayout = true;
    setNeedsDisplay();
    if (m_superview) { m_superview->setNeedsLayout(); }
}

void View::setNeedsDisplay() {
    // The "needs redraw" state lives in Context: it owns the draw-list, so a view simply tells the
    // context that the frame is stale. A detached view (without a context) becomes dirty again
    // through setNeedsLayout on re-attach.
    if (m_context) { m_context->setNeedsRedraw(); }
}

void View::propagateThemeChanged() {
    restyle();
    for (size_t i = 0; i < m_subviews.size(); ++i) { // a themeChanged() may rebuild a subtree
        const std::shared_ptr<View> subview = m_subviews[i];
        subview->propagateThemeChanged();
    }
}

void View::setHidden(bool hidden) {
    if (m_isHidden == hidden) { return; }
    m_isHidden = hidden;
    if (m_isHidden) { Context::forgetViewInAllContexts(this); }
    setNeedsDisplay();
}

void View::setFocusable(bool focusable) {
    if (m_isFocusable == focusable) { return; }
    m_isFocusable = focusable;
    if (!m_isFocusable) { Context::forgetFocusInAllContexts(this); }
}

void View::setUserInteractionEnabled(bool enabled) {
    if (m_isUserInteractionEnabled == enabled) { return; }
    m_isUserInteractionEnabled = enabled;
    if (!m_isUserInteractionEnabled) { Context::forgetViewInAllContexts(this); }
}

void View::setUsesIntrinsicContentSize(bool usesIntrinsicContentSize) {
    if (!m_internal) { return; }
    // The flip side of the same restriction: a node cannot be made a measure-leaf if it already has
    // children (Yoga would install the measure function on a node with children → the same
    // std::logic_error). A measure-leaf is a leaf.
    assert(
        !(usesIntrinsicContentSize && !m_subviews.empty()) &&
        "VIEW WITH CHILDREN CANNOT BECOME A MEASURE-LEAF (intrinsic content size) — measure-leaf must stay a leaf"
    );
    m_internal->usesIntrinsicContentSize = usesIntrinsicContentSize;
    configureYogaNode();
    invalidateIntrinsicContentSize();
}

void View::invalidateIntrinsicContentSize() {
    if (m_internal && m_internal->ref && YGNodeHasMeasureFunc(m_internal->ref)) { YGNodeMarkDirty(m_internal->ref); }
    setNeedsLayout();
}

void View::configureYogaNode() {
    if (!m_internal || !m_internal->ref) { return; }
    YGNodeSetContext(m_internal->ref, this);
    YGNodeSetMeasureFunc(m_internal->ref, m_internal->usesIntrinsicContentSize ? measureView : nullptr);
}

void View::setOpacity(float opacity) {
    m_surface.setOpacity(opacity);
    if (m_surface.opacity() <= 0.f) { Context::forgetViewInAllContexts(this); }
}

void View::setEnabled(bool enabled) {
    if (m_isEnabled == enabled) { return; }
    m_isEnabled = enabled;
    enabledChanged(enabled);
    if (!m_isEnabled) { Context::forgetViewInAllContexts(this); }
    setNeedsDisplay();
}

bool View::canBecomeFocused() const {
    return m_isFocusable && m_isEnabled && !m_isHidden && m_isUserInteractionEnabled && m_surface.opacity() > 0.f;
}

void View::setFocused(bool focused) {
    if (m_isFocused == focused) { return; }
    m_isFocused = focused;
    focusChanged(focused);
    setNeedsDisplay();
}

Rect View::getWorldFrame() const {
    Rect result = m_frame;
    result.origin.x += m_frameOffset.x;
    result.origin.y += m_frameOffset.y;
    const View *parent = m_superview;
    while (parent) {
        result.origin.x += parent->m_frame.origin.x + parent->m_frameOffset.x;
        result.origin.y += parent->m_frame.origin.y + parent->m_frameOffset.y;
        parent = parent->m_superview;
    }
    return result;
}

bool View::pointInside(Point windowPosition) const {
    Rect frame = getWorldFrame();
    return windowPosition.x >= frame.origin.x && windowPosition.y >= frame.origin.y &&
           windowPosition.x <= frame.origin.x + frame.size.width &&
           windowPosition.y <= frame.origin.y + frame.size.height;
}

View *View::hitTest(Point windowPosition) {
    if (m_isHidden || !m_isEnabled || !m_isUserInteractionEnabled || m_surface.opacity() <= 0.f) { return nullptr; }
    if (!pointInside(windowPosition)) { return nullptr; }

    for (auto it = m_subviews.rbegin(); it != m_subviews.rend(); ++it) {
        View *hitView = (*it)->hitTest(windowPosition);
        if (hitView) { return hitView; }
    }
    return this;
}

void View::render(float offsetX, float offsetY) {
    if (m_isHidden || m_surface.opacity() <= 0.f) { return; }
    Context *context = getContext();
    if (!context) { return; }

    const Point origin(m_frame.origin.x + m_frameOffset.x, m_frame.origin.y + m_frameOffset.y);
    const Point worldOrigin(offsetX + origin.x, offsetY + origin.y);
    bool pushedClip = false;
    if (m_surface.clipsToBounds()) {
        Rect clipRect;
        clipRect.origin = worldOrigin;
        clipRect.size = m_frame.size;
        context->getRenderer().pushClipRect(clipRect);
        pushedClip = true;
    }

    m_surface.draw(context->getRenderer(), Rect(worldOrigin, m_frame.size), m_transform);

    draw(offsetX + m_frameOffset.x, offsetY + m_frameOffset.y);

    for (const auto &subview : m_subviews) {
        subview->render(worldOrigin.x, worldOrigin.y);
    }
    if (pushedClip) { context->getRenderer().popClipRect(); }
}

void View::setFrameOffset(Point offset) {
    if (m_frameOffset.x == offset.x && m_frameOffset.y == offset.y) { return; }
    m_frameOffset = offset;
    setNeedsDisplay();
}

} // namespace PandaUI
