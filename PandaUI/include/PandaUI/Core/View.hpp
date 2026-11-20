#pragma once

#include "PandaUI/Core/Accessibility.hpp"
#include "PandaUI/Core/Base.hpp"
#include "PandaUI/Core/Cursor.hpp"
#include "PandaUI/Core/DragAndDrop.hpp"
#include "PandaUI/Core/KeyboardEvent.hpp"
#include "PandaUI/Layout/Layout.hpp"
#include "PandaUI/Core/PointerEvent.hpp"
#include "PandaUI/Rendering/Surface.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace PandaUI {

class Context;
struct ViewInternal;

class View : public std::enable_shared_from_this<View> {
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
    // A theme token (ThemeColor::surface) follows the theme by itself; a Color is a snapshot.
    void setBackgroundColor(ThemeColor color) {
        m_surface.setBackgroundColor(color);
    }
    Color getBackgroundColor() const {
        return m_surface.backgroundColor();
    }
    // Not drawn and not hit, the place in the layout is kept.
    void setHidden(bool hidden);
    bool isHidden() const {
        return m_isHidden;
    }
    // Hidden and out of the layout: the view takes no place until it is brought back (a row of a
    // form that does not apply now, a banner with nothing to say).
    void setCollapsed(bool collapsed);
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
    // The view of this subtree — this one, or one inside it — that had the keyboard focus last;
    // null when none had it, or when that view is gone. What a container gives the focus back to
    // when its content is shown again (TabView), so that coming back is coming back to the place.
    std::shared_ptr<View> lastFocusedInside() const {
        return m_lastFocusedInside.lock();
    }
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
    /* ------------------------------ Accessibility ------------------------------ */
    // The name a UI test finds the view by, given by the code that builds the screen (the
    // accessibilityIdentifier of UIKit). Shown nowhere; empty by default.
    void setAccessibilityId(std::string id) {
        m_accessibilityId = std::move(id);
    }
    const std::string &getAccessibilityId() const {
        return m_accessibilityId;
    }
    // What the view is and what it shows now (Core/Accessibility.hpp). A control fills its role,
    // its label and its value; a plain container leaves the description as it is.
    virtual void describeAccessibility(Accessibility &accessibility) const {}
    // A view that makes its rows on demand (a tree, a collection: only the rows in sight exist)
    // scrolls to the row that goes by `name`: the row exists after the next update. false — it
    // has no such row.
    virtual bool revealAccessibilityElement(const std::string &name) {
        return false;
    }
    void setClipsToBounds(bool clipsToBounds) {
        m_surface.setClipsToBounds(clipsToBounds);
    }
    bool clipsToBounds() const {
        return m_surface.clipsToBounds();
    }
    // How much of the view is there, 0..1 — with everything in it: its text or picture and its
    // subviews fade with it. At 0 the view is not drawn and takes no input.
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
    // A finger that came down on this view or on what it holds is offered to it, nearest view
    // first (Context): at the down with `moved` zero, and once the finger moved past a few points
    // with how far it went. A view that scrolls with a finger takes it (true) — as a UIScrollView
    // takes a pan that began on a button: it gets the moves and the release of that finger from then
    // on, and the views under the finger that had the press get interactionCancelled (a button lets
    // go without its action). A scroll view still moving takes it at the down: the touch stops it and
    // presses nothing.
    virtual bool takesTouch(const PointerEvent &event, Point moved) {
        return false;
    }
    // A view that works with the moving finger it was pressed with — a slider, text being selected
    // — keeps it: no scroll view around it is offered the finger.
    virtual bool keepsTouch() const {
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
    // Called when the theme changes. Stored colors need nothing: a ThemeColor token resolves at
    // draw time. Override for state DERIVED from the theme (a texture, a cached palette, a view
    // rebuilt from its model).
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
    void restyle(); // themeChanged(), stamped with the current theme generation
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
    std::weak_ptr<View> m_lastFocusedInside; // kept by the context: Context::setFocusedView
    Cursor m_cursor;
    std::string m_tooltip;
    std::string m_accessibilityId;
    uint64_t m_themeGeneration; // the theme this view was last styled under
    void render(float offsetX, float offsetY);
    friend class Layout;
    friend class Context;
    friend class ScrollView;
};

} // namespace PandaUI
