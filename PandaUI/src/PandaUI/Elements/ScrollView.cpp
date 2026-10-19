#include "PandaUI/Elements/ScrollView.hpp"

#include "PandaUI/Core/Profile.hpp"

#include <algorithm>
#include <cmath>
#include <memory>

namespace PandaUI {

namespace {

    constexpr float OVERSCROLL_LIMIT = 24.f;          // Visual soft limit for rubber-band stretch in points.
    constexpr float OVERSCROLL_RESISTANCE = 14.f;     // Higher values make overscroll harder to pull further.
    constexpr float OVERSCROLL_INPUT_VELOCITY = 60.f; // Converts overscroll input delta into spring velocity.
    constexpr float DRAG_INERTIA_VELOCITY = 40.f;     // Converts pointer drag delta into post-release scroll velocity.
    constexpr float SCROLL_DECELERATION = 4.5f;       // Exponential slowdown for drag inertia.
    constexpr float SCROLL_STOP_VELOCITY = 2.5f;      // Velocity threshold where inertial scrolling snaps to stop.
    constexpr float BOUNCE_STIFFNESS = 180.f;         // Spring strength pulling overscroll back to zero.
    constexpr float BOUNCE_DAMPING = 24.f;            // Spring damping; higher values reduce oscillation.
    constexpr float BOUNCE_SNAP_DISTANCE = 0.25f;     // Distance threshold where bounce snaps exactly to zero.
    constexpr float BOUNCE_SNAP_VELOCITY = 2.f;       // Velocity threshold where bounce snaps exactly to zero.

    float overscrollResistance(float overscroll) {
        const float normalized = std::abs(overscroll) / OVERSCROLL_LIMIT;
        return 1.f / (1.f + normalized * OVERSCROLL_RESISTANCE);
    }

    float applyOverscrollResistance(float delta, float overscroll) {
        return delta * overscrollResistance(overscroll);
    }

} // namespace

ScrollView::ScrollView()
    : m_contentView(std::make_shared<View>())
    , m_contentSize()
    , m_contentOffset()
    , m_scrollVelocity()
    , m_overscrollOffset()
    , m_overscrollVelocity()
    , m_lastDragPosition()
    , m_dragPointerId(0)
    , m_scrollDirection(ScrollDirection::Vertical)
    , m_scrollEnabled(true)
    , m_bounces(true)
    , m_automaticallyAdjustsContentSize(true)
    , m_isDragging(false)
    , m_isScrollGestureActive(false)
    , m_ignoreMomentumUntilNextGesture(false) {
    setBackgroundColor(Color(0x00000000));
    setClipsToBounds(true);

    m_contentView->setBackgroundColor(Color(0x00000000));
    m_contentView->layout().setPositionType(PositionType::Absolute);
    m_contentView->layout().setFlexDirection(FlexDirection::Column);
    m_contentView->layout().setPosition(Edge::Left, Length::points(0.f));
    m_contentView->layout().setPosition(Edge::Top, Length::points(0.f));
    updateContentLayout();
    View::addSubview(m_contentView);
}

void ScrollView::addContentSubview(std::shared_ptr<View> view) {
    m_contentView->addSubview(view);
}

void ScrollView::setContentSize(Size size) {
    const bool sizeChanged = m_contentSize.width != size.width || m_contentSize.height != size.height;
    m_automaticallyAdjustsContentSize = false;
    m_contentSize = size;
    updateContentLayout();
    if (!sizeChanged) {
        applyContentOffset();
        return;
    }
    m_contentOffset = clampedContentOffset(m_contentOffset);
    if (!m_bounces) {
        m_scrollVelocity = Point();
        m_overscrollOffset = Point();
        m_overscrollVelocity = Point();
    }
    applyContentOffset();
}

void ScrollView::setAutomaticallyAdjustsContentSize(bool automaticallyAdjustsContentSize) {
    if (m_automaticallyAdjustsContentSize == automaticallyAdjustsContentSize) { return; }
    m_automaticallyAdjustsContentSize = automaticallyAdjustsContentSize;
    updateContentLayout();
    if (!m_automaticallyAdjustsContentSize) {
        m_contentOffset = clampedContentOffset(m_contentOffset);
        applyContentOffset();
    }
}

void ScrollView::setContentOffset(Point offset) {
    const bool measured = m_contentSize.width > 0.f || m_contentSize.height > 0.f;
    m_hasPendingContentOffset = !measured;
    if (!measured) { m_pendingContentOffset = offset; }
    setContentOffsetInternal(offset);
}

void ScrollView::setScrollDirection(ScrollDirection direction) {
    if (m_scrollDirection == direction) { return; }
    m_scrollDirection = direction;
    updateContentLayout();
    setContentOffsetInternal(m_contentOffset);
}

void ScrollView::setBounces(bool bounces) {
    m_bounces = bounces;
    if (!m_bounces) {
        m_overscrollOffset = Point();
        m_overscrollVelocity = Point();
        applyContentOffset();
    }
}

bool ScrollView::pointerDown(PointerEvent &event) {
    if (!m_scrollEnabled) { return false; }
    if (event.type == PointerType::Mouse && event.button != PointerButton::Left) { return false; }
    m_isDragging = true;
    m_isScrollGestureActive = false;
    m_ignoreMomentumUntilNextGesture = false;
    m_scrollVelocity = Point();
    m_overscrollVelocity = Point();
    m_dragPointerId = event.pointerId;
    m_lastDragPosition = event.position;
    return true;
}

bool ScrollView::pointerMove(PointerEvent &event) {
    if (!m_scrollEnabled || !m_isDragging || event.pointerId != m_dragPointerId) { return false; }
    const Point delta(event.position.x - m_lastDragPosition.x, event.position.y - m_lastDragPosition.y);
    m_lastDragPosition = event.position;
    const Point scrollDelta(allowsHorizontalScroll() ? -delta.x : 0.f, allowsVerticalScroll() ? -delta.y : 0.f);
    const ScrollResult result = scrollBy(scrollDelta);
    m_scrollVelocity = result.createdOverscroll
                           ? Point()
                           : Point(scrollDelta.x * DRAG_INERTIA_VELOCITY, scrollDelta.y * DRAG_INERTIA_VELOCITY);
    return true;
}

bool ScrollView::pointerUp(PointerEvent &event) {
    if (!m_isDragging || event.pointerId != m_dragPointerId) { return false; }
    m_isDragging = false;
    return true;
}

bool ScrollView::scroll(ScrollEvent &event) {
    if (!m_scrollEnabled) { return false; }
    if (event.isMomentum) {
        m_isScrollGestureActive = false;
    } else if (event.isTrackpad) {
        m_isScrollGestureActive = !event.isGestureEnded;
    } else {
        m_isScrollGestureActive = false;
    }

    if (!event.isMomentum) {
        m_scrollVelocity = Point();
        m_ignoreMomentumUntilNextGesture = false;
    }
    if (event.isMomentum && m_ignoreMomentumUntilNextGesture) { return true; }

    const float scrollScale = event.isTrackpad ? 48.f : 42.f;
    float deltaX = event.delta.x;
    // Horizontal-only scroll (tab bar): the vertical mouse wheel is mapped to the horizontal axis —
    // a mouse has no horizontal axis; a trackpad gesture (with a real X delta) is left untouched.
    if (allowsHorizontalScroll() && !allowsVerticalScroll() && deltaX == 0.f) { deltaX = event.delta.y; }
    const ScrollResult result = scrollBy(Point(
        allowsHorizontalScroll() ? -deltaX * scrollScale : 0.f,
        allowsVerticalScroll() ? -event.delta.y * scrollScale : 0.f
    ));

    if (event.isMomentum && result.createdOverscroll) { m_ignoreMomentumUntilNextGesture = true; }

    return result.moved;
}

void ScrollView::update(double deltaTime) {
    PANDA_UI_PROFILE_SCOPE_N("PandaUI::ScrollView::update");
    if (m_isDragging) { return; }

    if (hasScrollVelocity()) {
        const float dt = static_cast<float>(deltaTime);
        const ScrollResult result = scrollBy(Point(m_scrollVelocity.x * dt, m_scrollVelocity.y * dt));
        if (result.createdOverscroll) {
            m_scrollVelocity = Point();
        } else {
            updateScrollVelocityAxis(m_scrollVelocity.x, deltaTime);
            updateScrollVelocityAxis(m_scrollVelocity.y, deltaTime);
        }
    }

    if (m_isScrollGestureActive || !hasOverscroll()) { return; }

    updateOverscrollAxis(m_overscrollOffset.x, m_overscrollVelocity.x, deltaTime);
    updateOverscrollAxis(m_overscrollOffset.y, m_overscrollVelocity.y, deltaTime);
    applyContentOffset();
}

void ScrollView::viewDidCalculateLayout() {
    syncAutomaticContentSize();
    if (m_hasPendingContentOffset &&
        (m_contentSize.width > 0.f || m_contentSize.height > 0.f)) {
        m_hasPendingContentOffset = false;
        m_contentOffset = m_pendingContentOffset;
    }
    // The viewport may have grown (window resize/fullscreen) with unchanged content — the offset
    // is re-clamped to the new maximum, otherwise the content stays shifted for no reason.
    const Point clamped = clampedContentOffset(m_contentOffset);
    if (clamped.x != m_contentOffset.x || clamped.y != m_contentOffset.y) { m_contentOffset = clamped; }
    applyContentOffset();
}

bool ScrollView::allowsHorizontalScroll() const {
    return m_scrollDirection == ScrollDirection::Horizontal || m_scrollDirection == ScrollDirection::Both;
}

bool ScrollView::allowsVerticalScroll() const {
    return m_scrollDirection == ScrollDirection::Vertical || m_scrollDirection == ScrollDirection::Both;
}

float ScrollView::maxContentOffsetX() {
    return std::max(0.f, m_contentSize.width - getFrame().size.width);
}

float ScrollView::maxContentOffsetY() {
    return std::max(0.f, m_contentSize.height - getFrame().size.height);
}

bool ScrollView::hasOverscroll() const {
    return m_overscrollOffset.x != 0.f || m_overscrollOffset.y != 0.f;
}

bool ScrollView::hasScrollVelocity() const {
    return m_scrollVelocity.x != 0.f || m_scrollVelocity.y != 0.f;
}

Point ScrollView::clampedContentOffset(Point offset) {
    return Point(
        allowsHorizontalScroll() ? std::clamp(offset.x, 0.f, maxContentOffsetX()) : 0.f,
        allowsVerticalScroll() ? std::clamp(offset.y, 0.f, maxContentOffsetY()) : 0.f
    );
}

ScrollView::ScrollResult ScrollView::applyScrollAxis(
    float &offset, float &overscroll, float &velocity, float delta, float maxOffset, bool isEnabled
) {
    const float previousOffset = offset;
    const float previousOverscroll = overscroll;

    if (!isEnabled) {
        offset = 0.f;
        overscroll = 0.f;
        velocity = 0.f;
        return {previousOffset != offset || previousOverscroll != overscroll, false};
    }

    if (delta == 0.f) { return {}; }

    if (!m_bounces) {
        const float nextOffset = std::clamp(offset + delta, 0.f, maxOffset);
        const bool moved = nextOffset != offset;
        offset = nextOffset;
        overscroll = 0.f;
        velocity = 0.f;
        return {moved, false};
    }

    if (overscroll != 0.f) {
        if (overscroll * delta > 0.f) {
            const float dampedDelta = applyOverscrollResistance(delta, overscroll);
            overscroll += dampedDelta;
            velocity = dampedDelta * OVERSCROLL_INPUT_VELOCITY;
            return {true, true};
        }

        const float nextOverscroll = overscroll + delta;
        if (overscroll * nextOverscroll > 0.f) {
            overscroll = nextOverscroll;
            velocity = delta * OVERSCROLL_INPUT_VELOCITY;
            return {true, false};
        }

        delta = nextOverscroll;
        overscroll = 0.f;
        velocity = 0.f;
    }

    const float requestedOffset = offset + delta;
    const float clampedOffset = std::clamp(requestedOffset, 0.f, maxOffset);
    const float overflow = requestedOffset - clampedOffset;
    offset = clampedOffset;

    if (overflow == 0.f) { return {previousOffset != offset || previousOverscroll != overscroll, false}; }

    const float dampedOverflow = applyOverscrollResistance(overflow, overscroll);
    overscroll += dampedOverflow;
    velocity = dampedOverflow * OVERSCROLL_INPUT_VELOCITY;
    return {true, true};
}

ScrollView::ScrollResult ScrollView::scrollBy(Point delta) {
    ScrollResult result;
    const ScrollResult horizontal = applyScrollAxis(
        m_contentOffset.x,
        m_overscrollOffset.x,
        m_overscrollVelocity.x,
        delta.x,
        maxContentOffsetX(),
        allowsHorizontalScroll()
    );
    const ScrollResult vertical = applyScrollAxis(
        m_contentOffset.y,
        m_overscrollOffset.y,
        m_overscrollVelocity.y,
        delta.y,
        maxContentOffsetY(),
        allowsVerticalScroll()
    );
    result.moved = horizontal.moved || vertical.moved;
    result.createdOverscroll = horizontal.createdOverscroll || vertical.createdOverscroll;
    if (result.moved) { applyContentOffset(); }
    return result;
}

void ScrollView::setContentOffsetInternal(Point offset) {
    m_contentOffset = clampedContentOffset(offset);
    m_scrollVelocity = Point();
    m_overscrollOffset = Point();
    m_overscrollVelocity = Point();
    applyContentOffset();
}

void ScrollView::updateScrollVelocityAxis(float &velocity, double deltaTime) {
    if (velocity == 0.f) { return; }

    velocity *= std::exp(-SCROLL_DECELERATION * static_cast<float>(deltaTime));
    if (std::abs(velocity) <= SCROLL_STOP_VELOCITY) { velocity = 0.f; }
}

void ScrollView::updateOverscrollAxis(float &overscroll, float &velocity, double deltaTime) {
    if (overscroll == 0.f && velocity == 0.f) { return; }

    const float dt = static_cast<float>(deltaTime);
    const float acceleration = -BOUNCE_STIFFNESS * overscroll - BOUNCE_DAMPING * velocity;
    velocity += acceleration * dt;
    overscroll += velocity * dt;

    if (std::abs(overscroll) <= BOUNCE_SNAP_DISTANCE && std::abs(velocity) <= BOUNCE_SNAP_VELOCITY) {
        overscroll = 0.f;
        velocity = 0.f;
    }
}

void ScrollView::updateContentLayout() {
    if (!m_automaticallyAdjustsContentSize) {
        m_contentView->layout().setSize(m_contentSize);
        return;
    }

    switch (m_scrollDirection) {
        case ScrollDirection::Vertical:
            m_contentView->layout().setWidth(Length::percent(100.f));
            m_contentView->layout().setHeight(Length::autoValue());
            break;
        case ScrollDirection::Horizontal:
            m_contentView->layout().setWidth(Length::autoValue());
            m_contentView->layout().setHeight(Length::percent(100.f));
            break;
        case ScrollDirection::Both:
            m_contentView->layout().setWidth(Length::autoValue());
            m_contentView->layout().setHeight(Length::autoValue());
            break;
    }
}

void ScrollView::syncAutomaticContentSize() {
    if (!m_automaticallyAdjustsContentSize) { return; }

    Size size = m_contentView->getFrame().size;
    if (!allowsHorizontalScroll()) { size.width = getFrame().size.width; }
    if (!allowsVerticalScroll()) { size.height = getFrame().size.height; }

    if (m_contentSize.width == size.width && m_contentSize.height == size.height) { return; }

    m_contentSize = size;
    m_contentOffset = clampedContentOffset(m_contentOffset);
    if (!m_bounces) {
        m_scrollVelocity = Point();
        m_overscrollOffset = Point();
        m_overscrollVelocity = Point();
    }
}

void ScrollView::applyContentOffset() {
    const Point drawOffset(m_contentOffset.x + m_overscrollOffset.x, m_contentOffset.y + m_overscrollOffset.y);
    m_contentView->setFrameOffset(Point(-drawOffset.x, -drawOffset.y));
}

} // namespace PandaUI
