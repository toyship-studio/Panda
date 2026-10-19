#pragma once

#include "PandaUI/Core/View.hpp"

#include <memory>

namespace PandaUI {

enum class ScrollDirection {
    Vertical,
    Horizontal,
    Both,
};

class ScrollView : public View {
public:
    ScrollView();

    void addContentSubview(std::shared_ptr<View> view);
    std::shared_ptr<View> getContentView() const {
        return m_contentView;
    }

    void setContentSize(Size size);
    Size getContentSize() const {
        return m_contentSize;
    }
    void setAutomaticallyAdjustsContentSize(bool automaticallyAdjustsContentSize);
    bool automaticallyAdjustsContentSize() const {
        return m_automaticallyAdjustsContentSize;
    }
    // Before the first layout the content size is unknown and the offset is clamped to 0 —
    // the request is remembered and applied once the content is measured (restoring
    // the scroll position of a recreated panel).
    void setContentOffset(Point offset);
    Point getContentOffset() const {
        return m_contentOffset;
    }
    void setScrollEnabled(bool enabled) {
        m_scrollEnabled = enabled;
    }
    bool isScrollEnabled() const {
        return m_scrollEnabled;
    }
    void setScrollDirection(ScrollDirection direction);
    ScrollDirection getScrollDirection() const {
        return m_scrollDirection;
    }
    void setBounces(bool bounces);
    bool getBounces() const {
        return m_bounces;
    }

    bool pointerDown(PointerEvent &event) override;
    bool pointerMove(PointerEvent &event) override;
    bool pointerUp(PointerEvent &event) override;
    bool scroll(ScrollEvent &event) override;
    void update(double deltaTime) override;

protected:
    void viewDidCalculateLayout() override;

private:
    struct ScrollResult {
        bool moved = false;
        bool createdOverscroll = false;
    };

    std::shared_ptr<View> m_contentView;
    Size m_contentSize;
    Point m_contentOffset;
    Point m_pendingContentOffset;
    bool m_hasPendingContentOffset = false;
    Point m_scrollVelocity;
    Point m_overscrollOffset;
    Point m_overscrollVelocity;
    Point m_lastDragPosition;
    int m_dragPointerId;
    ScrollDirection m_scrollDirection;
    bool m_scrollEnabled;
    bool m_bounces;
    bool m_automaticallyAdjustsContentSize;
    bool m_isDragging;
    bool m_isScrollGestureActive;
    bool m_ignoreMomentumUntilNextGesture;

    bool allowsHorizontalScroll() const;
    bool allowsVerticalScroll() const;
    float maxContentOffsetX();
    float maxContentOffsetY();
    bool hasOverscroll() const;
    bool hasScrollVelocity() const;
    Point clampedContentOffset(Point offset);
    ScrollResult
    applyScrollAxis(float &offset, float &overscroll, float &velocity, float delta, float maxOffset, bool isEnabled);
    ScrollResult scrollBy(Point delta);
    void setContentOffsetInternal(Point offset);
    void updateScrollVelocityAxis(float &velocity, double deltaTime);
    void updateOverscrollAxis(float &overscroll, float &velocity, double deltaTime);
    void updateContentLayout();
    void syncAutomaticContentSize();
    void applyContentOffset();
};

} // namespace PandaUI
