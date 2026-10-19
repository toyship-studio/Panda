#pragma once

#include "Panda/Terrain/TerrainData2D.hpp"

namespace Panda {

enum class TerrainBrushMode2D {
    Raise,   // raise/lower the surface (direction = subtract lowers)
    Flatten, // level to the height of the stroke's start point
    Smooth,  // smooth toward the average of the neighbors
    Paint,   // paint the vertices with a material layer (1..LAYER_COUNT)
};

// Rectangle of the affected grid VERTICES — for chunk invalidation and the undo snapshot of a stroke.
struct TerrainBrushRegion {
    bool touched = false;
    uint32_t minX = 0;
    uint32_t minZ = 0;
    uint32_t maxX = 0;
    uint32_t maxZ = 0;

    void extendVertex(uint32_t x, uint32_t z);
    void extendRegion(const TerrainBrushRegion &other);
};

// Relief brushes — paint-flow: applyTick is called every frame of a held stroke,
// the edit amount = strength × deltaTime × falloff (smoothstep toward the circle edge).
// A pure function over the data: applying to meshes/undo is up to the caller.
namespace TerrainBrush2D {

    // centerX/centerZ — the brush point in terrain local coordinates (meters);
    // targetHeight — the Flatten target (the height of the stroke's start point), ignored by the
    // other modes.
    TerrainBrushRegion applyTick(
        TerrainData2D &data,
        TerrainBrushMode2D mode,
        bool subtract,
        float centerX,
        float centerZ,
        float radius,
        float strength,
        float targetHeight,
        float deltaTime
    );

    // The painted flora brush: a continuous density of an RGBA mask channel (add/erase), paint-flow
    // strength × deltaTime × falloff — the soft edge of a stroke gives a natural thinning of plants.
    TerrainBrushRegion applyFloraPaint(
        TerrainData2D &data,
        float centerX,
        float centerZ,
        float radius,
        uint32_t channel,
        bool erase,
        float strength,
        float deltaTime
    );

    // Painting with a material layer — a sprayer: a vertex is painted with the probability
    // strength × deltaTime × falloff (denser at the center, sparser at the edge; the layer is
    // discrete — the feathering is done by the mesher over the neighborhood). seed — the stroke's
    // tick counter (a deterministic hash).
    TerrainBrushRegion applyPaint(
        TerrainData2D &data,
        float centerX,
        float centerZ,
        float radius,
        uint8_t layer,
        float strength,
        float deltaTime,
        uint32_t seed
    );

    const char *modeName(TerrainBrushMode2D mode);

} // namespace TerrainBrush2D

} // namespace Panda
