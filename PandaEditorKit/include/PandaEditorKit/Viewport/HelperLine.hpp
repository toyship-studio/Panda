#pragma once

#include <PandaUI/Core/Base.hpp>

namespace Panda {

// A viewport helper line in ABSOLUTE window coordinates — ready to be drawn by the PandaUI overlay
// (the world→screen projection is already done, as for the gizmo). For example, an edge of a
// collider's wireframe box or a ring of the brush halo.
struct HelperLine {
    PandaUI::Point a;
    PandaUI::Point b;
    PandaUI::Color color;

    bool operator==(const HelperLine &other) const {
        return a.x == other.a.x && a.y == other.a.y && b.x == other.b.x && b.y == other.b.y &&
               color.r == other.color.r && color.g == other.color.g && color.b == other.color.b &&
               color.a == other.color.a;
    }
};

} // namespace Panda
