#pragma once

namespace Panda {

enum class RenderingMode {
    // Default render
    DEFAULT,
    // The geometry alone, as a mask of its silhouette: no material, no depth test, both sides.
    // A tool outlines what it draws this way (the selection of the editor)
    GEOMETRY_ONLY,
    // Rendering ids for mouse picking
    IDS,
    // Depth-only pass into a cascade's shadow map (CSM): engine depth shaders, no color is written
    SHADOW_DEPTH
};

}
