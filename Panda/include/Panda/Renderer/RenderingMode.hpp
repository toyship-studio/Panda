#pragma once

namespace Panda {

enum class RenderingMode {
    // Default render
    DEFAULT,
    // Rendering wiferame geometry to highlight selected objects
    GEOMETRY_ONLY,
    // Rendering ids for mouse picking
    IDS,
    // Depth-only pass into a cascade's shadow map (CSM): engine depth shaders, no color is written
    SHADOW_DEPTH
};

}
