#pragma once

#include <Rain/Rain.hpp>

namespace Panda {

// World settings of the terrain contribution (key "terrain" in .world,
// World::context<TerrainLodSettings>; TERRAIN section of the settings panel): LOD and draw
// distances (meters, terrain local space; drawDistance 0 — no limit) and widths of the outer tile
// rings (columns; 0 — the tier is disabled, detail drops off earlier).
struct TerrainLodSettings final : public Rain::Codable {
    float lod1 = 120.f;
    float lod2 = 240.f;
    float drawDistance = 0.f;
    // The ring widths are kept roughly proportional to the tier span: the number of tiles in a ring
    // is ≈ 8·R·W/span², so a ring that is wide in columns at a small span produces more submits
    // than the whole core. With 8/32/64 the 8×8 tier alone produced more draw calls than the
    // core (4 km map: 171 versus 144); 4/16/32 removes ~27% of the submits, and narrowing further
    // is pointless — the rings run into the map edge.
    int ring2Width = 4;  // tier of 4×4 tiles (step 4)
    int ring3Width = 16; // tier of 8×8 tiles (step 8)
    int ring4Width = 32; // tier of 16×16 tiles (step 16); beyond it the 32×32 tier up to the edge

    RAIN_FIELDS_BEGIN(TerrainLodSettings)
    RAIN_FIELD(lod1)
    RAIN_FIELD(lod2)
    RAIN_FIELD(drawDistance)
    RAIN_FIELD(ring2Width)
    RAIN_FIELD(ring3Width)
    RAIN_FIELD(ring4Width)
    RAIN_FIELDS_END
};

} // namespace Panda
