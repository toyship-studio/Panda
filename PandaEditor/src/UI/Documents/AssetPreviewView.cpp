#include "UI/Documents/AssetPreviewView.hpp"

#include "Preview/AssetPreviews.hpp"
#include "Preview/PreviewStage.hpp"
#include "Project/Loader/AssetHandlerEditor.hpp"

#include <Panda/GameLogic/GameContext.hpp>
#include <PandaEditorKit/Widgets/Typography.hpp>

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>

namespace Editor {

using namespace PandaUI;

namespace {

    constexpr float PREVIEW_HEIGHT = 220.f;
    constexpr float IMAGE_INSET = 8.f;         // an image stands off the edge of the plate
    // The turn of the camera per point of a drag or of a swipe, degrees: a drag across the
    // preview turns the subject about half way round.
    constexpr float ORBIT_SPEED = 0.45f;
    constexpr float SWIPE_POINTS = 40.f;       // a scroll unit of the trackpad in points (the Scene surface uses the same)
    constexpr float WHEEL_ZOOM = 0.12f;        // per notch
    constexpr float MIN_ZOOM = 0.25f;
    constexpr float MAX_ZOOM = 12.f;
    const float MAX_PITCH = glm::radians(89.f);

    // The primitive a new material preview starts on: the last one chosen in this session.
    Panda::PrimitiveMeshKind s_shape = Panda::PrimitiveMeshKind::Sphere;

    struct Shape {
        Panda::PrimitiveMeshKind kind;
        const char *title;
    };
    const Shape SHAPES[] = {
        {Panda::PrimitiveMeshKind::Sphere, "Sphere"},     {Panda::PrimitiveMeshKind::Cube, "Cube"},
        {Panda::PrimitiveMeshKind::Cylinder, "Cylinder"}, {Panda::PrimitiveMeshKind::Capsule, "Capsule"},
        {Panda::PrimitiveMeshKind::Plane, "Plane"},
    };

    const char *titleOf(Panda::PrimitiveMeshKind kind) {
        for (const Shape &shape : SHAPES) {
            if (shape.kind == kind) { return shape.title; }
        }
        return "Sphere";
    }

    Panda::AssetType typeOf(Panda::AssetId asset) {
        auto *handler = Panda::AssetHandlerEditor::current();
        if (handler == nullptr) { return Panda::AssetType::NONE; }
        const auto it = handler->getRegistry().find(asset);
        return it == handler->getRegistry().end() ? Panda::AssetType(Panda::AssetType::NONE) : it->second.type;
    }

} // namespace

AssetPreviewView::AssetPreviewView() {
    // The plate: the frame of the little viewport, also where the subject leaves it empty.
    setBackgroundColor(ThemeColor::inputDeep);
    surface().setCornerRadius(Radius::md);
    surface().setBorderWidth(1.f);
    surface().setBorderColor(ThemeColor::border);
    layout().setHeight(Length::points(PREVIEW_HEIGHT));
    layout().setMargin(Edge::Vertical, 6.f);
    setClipsToBounds(true);

    m_image = std::make_shared<ImageView>();
    m_image->setUserInteractionEnabled(false); // the plate takes the gestures
    m_image->layout().setPositionType(PositionType::Absolute);
    addSubview(m_image);

    m_shape = std::make_shared<MenuButton>(titleOf(s_shape));
    m_shape->setSize(ButtonSize::Small);
    m_shape->setTooltip("The primitive the material is shown on");
    m_shape->layout().setPositionType(PositionType::Absolute);
    m_shape->layout().setPosition(Edge::Top, Length::points(6.f));
    m_shape->layout().setPosition(Edge::Right, Length::points(6.f));
    addSubview(m_shape);
    rebuildShapeMenu();

    setCollapsed(true);
}

void AssetPreviewView::rebuildShapeMenu() {
    m_shape->clearItems();
    for (const Shape &shape : SHAPES) {
        const Panda::PrimitiveMeshKind kind = shape.kind;
        m_shape->addItem(shape.title, [this, kind] {
            s_shape = kind;
            m_shape->setText(titleOf(kind));
            if (m_lease) { m_lease->shape = kind; }
        });
    }
}

bool AssetPreviewView::isStage() const {
    return m_lease && m_lease->kind == Panda::AssetPreview::Kind::Stage;
}

void AssetPreviewView::setAsset(Panda::AssetId asset) {
    // The same asset keeps its preview — and the camera where the user left it: a form is bound
    // again on every edit.
    if (asset == m_asset && (m_lease || asset == 0)) { return; }
    m_asset = asset;
    auto *previews = Panda::AssetPreviews::shared();
    m_lease = previews && asset != 0 ? previews->lease(asset) : nullptr; // the old lease goes with its resources
    m_dragging = false;
    if (m_lease && m_lease->hasShape) { m_lease->shape = s_shape; }
    const bool stage = isStage();
    // A stage is drawn at the size it is shown at, edge to edge; an image keeps its proportions.
    m_image->setContentMode(stage ? ImageContentMode::Stretch : ImageContentMode::Fit);
    const float inset = stage ? 0.f : IMAGE_INSET;
    for (Edge edge : {Edge::Left, Edge::Top, Edge::Right, Edge::Bottom}) {
        m_image->layout().setPosition(edge, Length::points(inset));
    }
    const bool hasShape = m_lease && m_lease->hasShape;
    m_shape->setText(titleOf(s_shape));
    m_shape->setCollapsed(!hasShape);
    setCollapsed(m_lease == nullptr);
    applyTexture();
    setNeedsLayout();
}

void AssetPreviewView::applyTexture() {
    m_shown = m_lease ? m_lease->texture : TextureHandle();
    m_image->setTexture(m_shown);
    m_image->setFlipVertical(m_lease && m_lease->flipVertical);
    m_image->setHidden(!m_shown.isValid());
}

void AssetPreviewView::update(double deltaTime) {
    Panel::update(deltaTime);
    if (!m_lease) { return; }
    // The form is on screen (a hidden tab gets no update): the frame is asked for, at the pixels
    // it is shown at.
    m_lease->requested = true;
    if (isStage()) {
        const Rect frame = getWorldFrame();
        const Context *context = getContext();
        const Size dpi = context ? context->getDpiScale() : Size(1.f, 1.f);
        m_lease->width = static_cast<uint32_t>(std::max(frame.size.width * std::max(dpi.width, 1.f), 16.f));
        m_lease->height = static_cast<uint32_t>(std::max(frame.size.height * std::max(dpi.height, 1.f), 16.f));
    }
    // The texture of the lease appears with the first rendered frame, changes with the size of
    // the target and goes away when the project closes.
    if (!(m_lease->texture == m_shown)) { applyTexture(); }
}

void AssetPreviewView::orbitBy(float dx, float dy) {
    if (!isStage()) { return; }
    Panda::PreviewOrbit &orbit = m_lease->orbit;
    // The angles of the camera of the Scene: the pointer to the right turns the camera right,
    // and a camera that orbits moves left with it — the subject follows the hand.
    orbit.yaw -= glm::radians(dx * ORBIT_SPEED);
    orbit.pitch = std::clamp(orbit.pitch - glm::radians(dy * ORBIT_SPEED), -MAX_PITCH, MAX_PITCH);
}

void AssetPreviewView::zoomBy(float factor) {
    if (!isStage()) { return; }
    m_lease->orbit.zoom = std::clamp(m_lease->orbit.zoom * factor, MIN_ZOOM, MAX_ZOOM);
}

bool AssetPreviewView::pointerDown(PointerEvent &event) {
    if (!isStage() || (event.button != PointerButton::Left && event.button != PointerButton::Right)) { return false; }
    if (event.button == PointerButton::Left && event.clickCount >= 2) {
        // Back to the first view.
        m_lease->orbit = Panda::PreviewStage::defaultOrbit(typeOf(m_asset));
        return true;
    }
    m_dragging = true;
    m_dragButton = event.button;
    return true; // captured: the moves come until the release
}

bool AssetPreviewView::pointerMove(PointerEvent &event) {
    if (!m_dragging) { return false; }
    orbitBy(event.delta.x, event.delta.y);
    return true;
}

bool AssetPreviewView::pointerUp(PointerEvent &event) {
    if (!m_dragging || event.button != m_dragButton) { return false; }
    m_dragging = false;
    return true;
}

void AssetPreviewView::interactionCancelled() {
    m_dragging = false;
}

bool AssetPreviewView::scroll(ScrollEvent &event) {
    if (!isStage()) { return false; } // an image: the form scrolls
    if (event.magnification != 0.f) {
        zoomBy(1.f + event.magnification);
    } else if (event.isTrackpad) {
        orbitBy(event.delta.x * SWIPE_POINTS, event.delta.y * SWIPE_POINTS);
    } else {
        zoomBy(std::exp(event.delta.y * WHEEL_ZOOM));
    }
    return true;
}

} // namespace Editor
