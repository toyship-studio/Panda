#include "UI/Panels/ViewportPanel.hpp"
#include "UI/Commands.hpp"

#include <Foundation/PlatformDetection.hpp>

#include <PandaEditorKit/Widgets/Typography.hpp>
#include "PandaUI/Rendering/DrawList.hpp"
#include "PandaUI/Rendering/UIRenderer.hpp"
#include "PandaUI/Runtime/Context.hpp"

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Editor {

using namespace PandaUI;

namespace {

    // Floating viewport overlay bar: positioned absolutely (out of the flow, on top of the canvas) and
    // CONSUMES clicks on its background — otherwise a click on the bar would go to the viewport's
    // pointerDown and clear the selection.
    class OverlayBar final : public Panel {
    public:
        OverlayBar() {
            layout().setPositionType(PositionType::Absolute); // absolute: does not affect canvas centering
            layout().setPosition(Edge::Left, Length::points(12.f));
            layout().setPosition(Edge::Top, Length::points(12.f));
            layout().setFlexDirection(FlexDirection::Row);
            layout().setAlignItems(Align::Center);
            layout().setGap(3.f);
            layout().setPadding(4.f);
            surface().setCornerRadius(Radius::lg);
            surface().setBorderWidth(1.f);
        }

    protected:
        bool pointerDown(PointerEvent &) override {
            return true; // consume the click on the bar background — keep it from the viewport background
        }
    };

    // Vertical 1×18 divider inside the overlay.
    std::shared_ptr<Panel> makeOverlayDivider() {
        auto divider = std::make_shared<Panel>();
        divider->setUserInteractionEnabled(false);
        divider->layout().setSize({1.f, 18.f});
        divider->layout().setMargin(Edge::Horizontal, 3.f);
        divider->setBackgroundColor(ThemeColor::borderMed);
        return divider;
    }

    // The helper lines go into the draw list of the UI frame, and its size is fixed and shared with
    // the whole editor UI (DrawList.hpp; an overflow is an assert). The lines fill the list at most
    // to a half — the rest belongs to the views drawn after the viewport.
    constexpr uint32_t HELPER_LINES_VERTICES_FILL = PandaUI::MAX_VERTICES_COUNT / 2;
    constexpr uint32_t HELPER_LINES_INDICES_FILL = PandaUI::MAX_INDICES_COUNT / 2;

    // A 1 pt line as a bare quad with square ends (so that the edges meeting in a corner close it).
    // UIRenderer::drawLine rounds the ends, which such a line does not show, and spends 18 vertices
    // and 42 indices on a line instead of 6 and 6.
    void drawHelperLine(UIRenderer &renderer, const Panda::HelperLine &line) {
        const float dx = line.b.x - line.a.x;
        const float dy = line.b.y - line.a.y;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length < 1e-4f) { return; }
        const float alongX = dx / length * 0.5f; // half of the thickness along the line
        const float alongY = dy / length * 0.5f;
        const Point a0(line.a.x - alongX + alongY, line.a.y - alongY - alongX);
        const Point a1(line.a.x - alongX - alongY, line.a.y - alongY + alongX);
        const Point b0(line.b.x + alongX + alongY, line.b.y + alongY - alongX);
        const Point b1(line.b.x + alongX - alongY, line.b.y + alongY + alongX);
        renderer.drawTriangle(a0, b0, b1, line.color);
        renderer.drawTriangle(b1, a1, a0, line.color);
    }

} // namespace

// Non-clickable layer above the scene: draws helper lines (collider outlines) from ready-made window
// coordinates — the world→screen projection is done by the engine, as for the gizmo
// (see ViewportHelperLine). The lines come in the order of importance: the ones that do not fit
// into the overlay's share of the draw list are the tail of the list.
class HelperLinesOverlay final : public Panel {
public:
    HelperLinesOverlay() {
        setBackgroundColor(Color(0x00000000));
        setUserInteractionEnabled(false);
        layout().setPositionType(PositionType::Absolute);
        layout().setPosition(Edge::Left, Length::points(0.f));
        layout().setPosition(Edge::Top, Length::points(0.f));
        layout().setPosition(Edge::Right, Length::points(0.f));
        layout().setPosition(Edge::Bottom, Length::points(0.f));
    }

    void setLines(std::vector<Panda::HelperLine> lines) {
        if (m_lines == lines) { return; }
        m_lines = std::move(lines);
        setNeedsDisplay();
    }

protected:
    void draw(float, float) override {
        if (m_lines.empty()) { return; }
        Context *context = getContext();
        if (!context) { return; }
        UIRenderer &renderer = context->getRenderer();
        for (const Panda::HelperLine &line : m_lines) {
            const UIRenderer::Statistics stats = renderer.getStats();
            if (stats.vertexCount > HELPER_LINES_VERTICES_FILL || stats.indexCount > HELPER_LINES_INDICES_FILL) {
                break;
            }
            drawHelperLine(renderer, line);
        }
    }

private:
    std::vector<Panda::HelperLine> m_lines;
};

ViewportPanelView::ViewportPanelView(ViewportPanelOutput *output)
    : m_output(output) {
    addSubviews();
    setupLayout();
    setupStyle();
}

void ViewportPanelView::addSubviews() {
    // The scene column: the surface fills it, the overlays sit on top of it (absolute).
    m_sceneHost = std::make_shared<Panel>();
    m_sceneHost->setBackgroundColor(Color(0x00000000));
    m_sceneHost->layout().setFlexGrow(1.f);
    m_sceneHost->layout().setFlexBasis(0.f);
    m_sceneHost->layout().setFlexDirection(FlexDirection::Column);
    m_sceneHost->layout().setAlignItems(Align::Stretch);
    addSubview(m_sceneHost);

    // Scene surface (fills the column) — camera/picking input. Overlay toolbar on top.
    m_scene = std::make_shared<ViewportView>(this);
    m_scene->setAccessibilityId("world"); // the surface of the World view, for a UI test
    m_sceneHost->addSubview(m_scene);

    m_helperLines = std::make_shared<HelperLinesOverlay>(); // helpers (colliders) — above the scene, below the gizmo
    m_sceneHost->addSubview(m_helperLines);

    m_gizmoOverlay = std::make_shared<GizmoOverlayView>(); // the gizmo is drawn above the scene, below the toolbar
    m_sceneHost->addSubview(m_gizmoOverlay);

    // Overlay toolbar: gizmo radio group (Move/Rotate/Scale) + camera reset. UI-only (no 3D engine).
    m_overlay = std::make_shared<OverlayBar>();

    const char *gizmoIcons[3] = {Ic::GizmoMove, Ic::GizmoRotate, Ic::GizmoScale};
    const char *gizmoIds[3] = {"move", "rotate", "scale"}; // the tooltip follows the keymap: a test names the id
    for (int i = 0; i < 3; ++i) {
        auto button = std::make_shared<IconButton>(gizmoIcons[i], Ic::GizmoSize, 28.f);
        button->setAccessibilityId(gizmoIds[i]);
        button->setOnClick([this, i] { applyGizmoMode(i); });
        m_overlay->addSubview(button);
        m_gizmoButtons.push_back(button);
    }
    selectGizmo(0); // Move is active by default

    // Space toggle (Local/World): one button that swaps its icon, like PandaEditor's gizmo space.
    m_spaceButton = std::make_shared<IconButton>(Ic::SpaceWorld, Ic::GizmoSize, 28.f);
    m_spaceButton->setTooltip("Gizmo Space: World");
    m_spaceButton->setOnClick([this] {
        m_worldSpace = !m_worldSpace;
        m_spaceButton->setIconName(m_worldSpace ? Ic::SpaceWorld : Ic::SpaceLocal);
        m_spaceButton->setTooltip(m_worldSpace ? "Gizmo Space: World" : "Gizmo Space: Local");
        m_output->viewportDidPickSpace(m_worldSpace);
    });
    m_overlay->addSubview(m_spaceButton);

    m_overlay->addSubview(makeOverlayDivider());

    auto home = std::make_shared<IconButton>(Ic::Home, Ic::GizmoSize, 28.f);
    home->setTooltip("Reset Camera");
    home->setOnClick([this] { m_output->viewportDidResetCamera(); }); // CameraController resets the camera
    m_overlay->addSubview(home);

    m_overlay->addSubview(makeOverlayDivider());

    // The display options of the viewport (the editor scope: what the viewport draws, nothing of
    // it is saved into the world) — a popover next to the button; its word says what it opens.
    auto display = std::make_shared<IconButton>(Ic::Configure, Ic::GizmoSize, 28.f);
    display->setTitle("Display");
    display->setTooltip("Display Options: what the viewport draws (an editor setting)");
    IconButton *displayPtr = display.get();
    display->setOnClick([this, displayPtr] { m_output->viewportDidRequestDisplayOptions(displayPtr); });
    m_overlay->addSubview(display);

    m_sceneHost->addSubview(m_overlay);

    // Edit mode banner: a full-width centering strip (does not catch input) + the banner itself
    // (OverlayBar consumes the click — do not clear the selection). Shown by the session.
    m_editBannerHost = std::make_shared<Panel>();
    m_editBannerHost->setBackgroundColor(Color(0x00000000));
    m_editBannerHost->setUserInteractionEnabled(false);
    m_editBannerHost->layout().setPositionType(PositionType::Absolute);
    m_editBannerHost->layout().setPosition(Edge::Left, Length::points(0.f));
    m_editBannerHost->layout().setPosition(Edge::Right, Length::points(0.f));
    m_editBannerHost->layout().setPosition(Edge::Top, Length::points(12.f));
    m_editBannerHost->layout().setFlexDirection(FlexDirection::Row);
    m_editBannerHost->layout().setJustifyContent(Justify::Center);

    auto banner = std::make_shared<OverlayBar>();
    // OverlayBar is absolute for the corner toolbar; in the centering strip the banner lives in flow.
    banner->layout().setPositionType(PositionType::Relative);
    banner->layout().setPadding(Edge::Left, 12.f);
    m_editBannerLabel = std::make_shared<Label>("");
    m_editBannerLabel->setFont(Typo::Caption);
    banner->addSubview(m_editBannerLabel);
    auto stop = std::make_shared<IconButton>(Ic::Stop, Ic::GizmoSize, 28.f);
    stop->setTooltip("Stop Editing");
    stop->setFixedColor(ThemeColor::danger);
    stop->setOnClick([this] { m_output->viewportDidRequestStopEditing(); });
    banner->addSubview(stop);
    m_editBanner = banner;
    m_editBannerHost->addSubview(m_editBanner);
    m_editBannerHost->setHidden(true);
    m_sceneHost->addSubview(m_editBannerHost);
}

void ViewportPanelView::setKeyHints(const Commands &commands) {
#ifdef PLATFORM_MACOS
    const std::string snap = " \xE2\x80\x94 \xE2\x8C\x98 snaps"; // " — ⌘ snaps"
#else
    const std::string snap = " \xE2\x80\x94 Ctrl snaps";
#endif
    static const char *const TITLES[3] = {"Move", "Rotate", "Scale"};
    static const char *const IDS[3] = {"gizmo.move", "gizmo.rotate", "gizmo.scale"};
    for (size_t i = 0; i < m_gizmoButtons.size() && i < 3; ++i) {
        m_gizmoButtons[i]->setTooltip(commands.hint(TITLES[i], IDS[i]) + snap);
    }
}

void ViewportPanelView::setEditingSession(bool active, const std::string &title) {
    if (m_editActive == active && (!active || m_editBannerLabel->getText() == title)) { return; }
    m_editActive = active;
    m_editBannerLabel->setText(title);
    m_editBannerHost->setHidden(!active);
    applyBorder();
}

void ViewportPanelView::setHelperLines(std::vector<Panda::HelperLine> lines) {
    m_helperLines->setLines(std::move(lines));
}

void ViewportPanelView::selectGizmo(int mode) {
    for (int i = 0; i < static_cast<int>(m_gizmoButtons.size()); ++i) {
        m_gizmoButtons[i]->setActive(i == mode);
    }
}

void ViewportPanelView::applyGizmoMode(int mode) {
    selectGizmo(mode);
    m_output->viewportDidPickGizmo(mode);
}

void ViewportPanelView::viewportViewDidRectSelect(Point start, Point current, bool additive, RectSelectPhase phase) {
    m_output->viewportDidRectSelect(start, current, additive, phase); // → GPU picking via the IDS-FBO (engine)
}

bool ViewportPanelView::canAcceptDrop(const DragInfo &drag) {
    const std::string &t = drag.payload.type;
    // What a drop here means something for: a model and a prefab become entities, a world opens.
    return !drag.payload.items.empty() && (t == "asset.mesh" || t == "asset.prefab" || t == "asset.world");
}

void ViewportPanelView::dragEntered(const DragInfo &) {
    setDropActive(true);
}

void ViewportPanelView::dragExited() {
    setDropActive(false);
}

bool ViewportPanelView::performDrop(const DragInfo &drag) {
    setDropActive(false);
    std::vector<std::uint32_t> ids;
    ids.reserve(drag.payload.items.size());
    for (std::uint64_t id : drag.payload.items) { ids.push_back(static_cast<std::uint32_t>(id)); }
    std::string kind = drag.payload.type; // «asset.mesh» → «mesh»
    const auto dot = kind.find('.');
    if (dot != std::string::npos) { kind = kind.substr(dot + 1); }
    m_output->viewportDidDropAssets(ids, kind);
    return true;
}

void ViewportPanelView::setDropActive(bool active) {
    if (m_dropActive == active) { return; }
    m_dropActive = active;
    applyBorder();
}

void ViewportPanelView::applyBorder() {
    // Accent border around the whole panel: drag-over of a compatible asset and the edit mode.
    surface().setBorderColor(ThemeColor::accent);
    surface().setBorderWidth(m_dropActive || m_editActive ? 2.f : 0.f);
    setNeedsDisplay();
}

void ViewportPanelView::setupLayout() {
    layout().setFlexDirection(FlexDirection::Row);
    layout().setAlignItems(Align::Stretch);
}

void ViewportPanelView::setupStyle() {
    setBackgroundColor(ThemeColor::windowBackground); // what shows before the first frame arrives
    if (m_overlay) {
        // A floating layer: the popover elevation (a hairline shadow, like GPUI's elevated surfaces).
        m_overlay->setBackgroundColor(ThemeColor::surface);
        m_overlay->surface().setBorderColor(ThemeColor::border);
        m_overlay->surface().setShadow(Shadow::popover);
    }
    if (m_editBanner) {
        // Same floating style; the accent border signals a special mode (paired with the panel border).
        m_editBanner->setBackgroundColor(ThemeColor::surface);
        m_editBanner->surface().setBorderColor(ThemeColor::accent);
        m_editBanner->surface().setShadow(Shadow::popover);
    }
}

} // namespace Editor
