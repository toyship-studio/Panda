#include "UI/Widgets/GizmoOverlay.hpp"

#include "Viewport/Gizmo/Gizmo.hpp"

namespace Editor {

using namespace PandaUI;

GizmoOverlayView::GizmoOverlayView() {
    setUserInteractionEnabled(false);          // gizmo input goes through Panda::Input, not through PandaUI events
    setBackgroundColor(Color(0x00000000));     // transparent background — else surface covers the viewport (was white)
    layout().setPositionType(PositionType::Absolute);
    layout().setPosition(Edge::Left, Length::points(0.f));
    layout().setPosition(Edge::Top, Length::points(0.f));
    layout().setPosition(Edge::Right, Length::points(0.f));
    layout().setPosition(Edge::Bottom, Length::points(0.f));
    setVisible(false);
}

void GizmoOverlayView::setVisible(bool visible) {
    setHidden(!visible);
    layout().setDisplay(visible ? Display::Flex : Display::None);
}

void GizmoOverlayView::update(double) {
    if (isHidden()) { return; }
    // UI caches the draw list — redraw only when the engine has actually rebuilt the gizmo geometry.
    const uint64_t generation = Panda::Gizmo::getDrawData().generation;
    if (generation != m_lastGeneration) {
        m_lastGeneration = generation;
        setNeedsDisplay();
    }
}

void GizmoOverlayView::draw(float, float) {
    // Gizmo coordinates are absolute screen (like PandaUI offset+frame.origin), so offset is ignored.
    Context *context = getContext();
    if (!context) { return; }
    UIRenderer &renderer = context->getRenderer();
    const Panda::Gizmo::DrawData &data = Panda::Gizmo::getDrawData();
    if (!data.vertices || !data.indices || data.indicesCount < 3) { return; }
    for (uint32_t i = 0; i + 2 < data.indicesCount; i += 3) {
        const Panda::Gizmo::DrawVertex &v0 = data.vertices[data.indices[i]];
        const Panda::Gizmo::DrawVertex &v1 = data.vertices[data.indices[i + 1]];
        const Panda::Gizmo::DrawVertex &v2 = data.vertices[data.indices[i + 2]];
        renderer.drawTriangle(
            Point(v0.position.x, v0.position.y),
            Point(v1.position.x, v1.position.y),
            Point(v2.position.x, v2.position.y),
            Color(v0.color.r, v0.color.g, v0.color.b, v0.color.a)
        );
    }
}

} // namespace Editor
