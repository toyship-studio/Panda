#pragma once

#include <PandaUI/PandaUI.hpp>

#include <cstdint>

namespace Editor {

// Draws the gizmo geometry (engine Gizmo::getDrawData — 2D triangles in screen coordinates) on top
// of the viewport. Input and manipulation are on the engine side (EditorLayer runs Gizmos::manipulate
// from Panda::Input); only the visibility arrives here. The geometry is read straight from the engine
// in draw() (the same frame, the gizmo has already been manipulated). Does not catch clicks.
class GizmoOverlayView final : public PandaUI::View {
public:
    GizmoOverlayView();
    void setVisible(bool visible); // no selection / PLAY → hidden (the engine decides)

protected:
    void update(double deltaTime) override; // redraw by Gizmo::DrawData::generation (the engine marks changes)
    void draw(float offsetX, float offsetY) override;

private:
    std::uint64_t m_lastGeneration = 0;
};

} // namespace Editor
