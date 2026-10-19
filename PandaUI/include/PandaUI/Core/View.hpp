//
// Created by Admin on 17.06.2022.
//

#pragma once

#include "PandaUI/Core/Base.hpp"
#include "PandaUI/Core/Cursor.hpp"
#include "PandaUI/Core/DragAndDrop.hpp"
#include "PandaUI/Core/KeyboardEvent.hpp"
#include "PandaUI/Layout/Layout.hpp"
#include "PandaUI/Core/PointerEvent.hpp"
#include "PandaUI/Rendering/Surface.hpp"

#include <memory>
#include <string>
#include <vector>

namespace PandaUI {

class Context;
struct ViewInternal;

class View {
public:
    View();
    View(Rect frame);
    virtual ~View();
    /* ------------------------------ Life cycle ------------------------------ */
    virtual void update(double deltaTime) {}
    virtual void viewWillCalculateLayout() {}
    virtual void viewDidCalculateLayout() {}

    /* ------------------------------ Hierarchy ------------------------------ */
    // IMPORTANT: a view with an intrinsic content size (measure-leaf — Label/TextField) must NOT have
    // children (Yoga limitation; caught by an assert in addSubview). Accessories/icons go into a
    // wrapper container.
    void addSubview(std::shared_ptr<View> view);
    void removeSubview(std::shared_ptr<View> view);
    void removeSubview(View *view);
    void removeAllSubviews();
    void removeFromSuperview();
    View *getSuperview() const {
        return m_superview;
    }
    Context *getContext() const {
        return m_context;
    }

    /* ------------------------------ Layout ------------------------------ */
    Layout &layout() {
        return m_layout;
    }
    const Layout &layout() const {
        return m_layout;
    }
    void layoutReset();
    void layoutSetSize(Size size);
    void layoutSetSizePercent(Size size);
    void layoutSetOrigin(Point point);
    void layoutSetOriginPercent(Point point);
    void layoutSetMargins(EdgeInsets margins);
    void layoutSetRelative();
    void layoutSetAbsolute();
    void layoutSetInsetsFromParent(EdgeInsets insets);

    /* ------------------------------ Transform ------------------------------ */
    void setTransform(const glm::mat4 &transform) {
        m_transform = transform;
        setNeedsDisplay();
    }
    void resetTransform() {
        m_transform = glm::mat4(1.f);
        setNeedsDisplay();
    }
    void setNeedsLayout();
    void setNeedsDisplay();
    // Draw offset relative to the layout position, without relayout (for animations/scroll).
    void setFrameOffset(Point offset);
    bool needsLayout() const {
        return m_needsLayout;
    }
    virtual Size intrinsicContentSize(Size availableSize = {}) const {
        return {};
    }
    /* ------------------------------ Properties ------------------------------ */
    Surface &surface() {
        return m_surface;
    }
    const Surface &surface() const {
        return m_surface;
    }
    Rect getFrame() const {
        return m_frame;
    }
    Rect getWorldFrame() const;
    void setBackgroundColor(Color color) {
        m_surface.setBackgroundColor(color);
    }
    const Color &getBackgroundColor() const {
        return m_surface.backgroundColor();
    }
    void setHidden(bool hidden);
    bool isHidden() const {
        return m_isHidden;
    }
    void setEnabled(bool enabled);
    bool isEnabled() const {
        return m_isEnabled;
    }
    void setFocusable(bool focusable);
    bool isFocusable() const {
        return m_isFocusable;
    }
    bool isFocused() const {
        return m_isFocused;
    }
    virtual bool canBecomeFocused() const;
    // Text input field (TextField etc.). Key/accelerator routing uses this to avoid intercepting
    // printable key combinations (e.g. Space) while the focus is in a text input.
    virtual bool isTextInput() const {
        return false;
    }
    // Popup content that grabs the keyboard as soon as it is shown (Menu). When a Popup is attached
    // to the context, it focuses such content — arrows/Enter/Escape go to it instead of staying
    // with the owner underneath the popup.
    virtual bool wantsFocusWhenPresented() const {
        return false;
    }
    void setUserInteractionEnabled(bool enabled);
    bool isUserInteractionEnabled() const {
        return m_isUserInteractionEnabled;
    }
    bool isHovered() const {
        return m_isHovered;
    }
    void setCursor(Cursor cursor) {
        m_cursor = cursor;
    }
    Cursor getCursor() const {
        return m_cursor;
    }
    // Hover tooltip. An empty string means no tooltip. The context shows it as an overlay bubble
    // after a delay and hides it when the cursor leaves.
    void setTooltip(std::string tooltip) {
        m_tooltip = std::move(tooltip);
    }
    const std::string &getTooltip() const {
        return m_tooltip;
    }
    void setClipsToBounds(bool clipsToBounds) {
        m_surface.setClipsToBounds(clipsToBounds);
    }
    bool clipsToBounds() const {
        return m_surface.clipsToBounds();
    }
    void setOpacity(float opacity);
    float getOpacity() const {
        return m_surface.opacity();
    }
    const std::vector<std::shared_ptr<View>> &getSubviews() const {
        return m_subviews;
    }
    virtual bool pointInside(Point windowPosition) const;
    virtual View *hitTest(Point windowPosition);
    virtual bool pointerDown(PointerEvent &event) {
        return false;
    }
    virtual bool pointerUp(PointerEvent &event) {
        return false;
    }
    virtual bool pointerMove(PointerEvent &event) {
        return false;
    }
    virtual bool scroll(ScrollEvent &event) {
        return false;
    }
    virtual bool keyDown(KeyEvent &event) {
        return false;
    }
    virtual bool keyUp(KeyEvent &event) {
        return false;
    }
    virtual bool textInput(TextInputEvent &event) {
        return false;
    }

    /* ------------------------------ Drag and drop ------------------------------ */
    // Source: return true and fill the payload to start dragging this view (called at the start
    // of a drag, after the movement threshold). localPosition is the grab point in view
    // coordinates. By default a view is not a source.
    virtual bool prepareDrag(DragPayload &payload, Point localPosition) {
        return false;
    }
    // Target: whether a drop is accepted here (checked on every move while looking for a target).
    virtual bool canAcceptDrop(const DragInfo &drag) {
        return false;
    }
    // Target highlight/indicator: cursor enter, movement inside, exit.
    virtual void dragEntered(const DragInfo &drag) {}
    virtual void dragUpdated(const DragInfo &drag) {}
    virtual void dragExited() {}
    // Perform the drop. Return true if accepted.
    virtual bool performDrop(const DragInfo &drag) {
        return false;
    }

    // Called when attached to a Context (added to a shown hierarchy or as an overlay).
    virtual void contextAttached() {}

    virtual void pointerEnter(PointerEvent &event) {}
    virtual void pointerLeave(PointerEvent &event) {}
    virtual void hoverChanged(bool hovered) {}
    virtual void enabledChanged(bool enabled) {}
    virtual void focusChanged(bool focused) {}
    virtual void interactionCancelled() {}
    // Called when the theme changes; override to re-read the colors from theme().
    virtual void themeChanged() {}
    // Calls themeChanged() for this view and its whole subtree.
    void propagateThemeChanged();

protected:
    virtual void draw(float offsetX, float offsetY) {}
    // Makes the view a measure-leaf (the Yoga measure function measures the content by
    // intrinsicContentSize). WARNING: such a view can NO LONGER have children — addSubview trips an
    // assert (see addSubview).
    void setUsesIntrinsicContentSize(bool usesIntrinsicContentSize);
    void invalidateIntrinsicContentSize();

    std::vector<std::shared_ptr<View>> m_subviews;

private:
    void setContext(Context *context);
    void resolveHierarchy();
    void updateHierarchy(double deltaTime);
    void notifyWillCalculateLayout();
    void calculateLayout();
    void applyCalculatedLayout();
    void configureYogaNode();
    void setFocused(bool focused);
    Rect m_frame;
    Point m_frameOffset;
    Layout m_layout;
    Surface m_surface;
    std::unique_ptr<ViewInternal> m_internal;
    View *m_superview;
    Context *m_context;
    glm::mat4 m_transform;
    bool m_needsLayout;
    bool m_isHidden;
    bool m_isEnabled;
    bool m_isFocusable;
    bool m_isFocused;
    bool m_isUserInteractionEnabled;
    bool m_isHovered;
    Cursor m_cursor;
    std::string m_tooltip;
    void render(float offsetX, float offsetY);
    friend class Layout;
    friend class Context;
    friend class ScrollView;
};

} // namespace PandaUI
