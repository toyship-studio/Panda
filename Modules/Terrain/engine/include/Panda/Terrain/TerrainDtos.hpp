#pragma once

#include "Panda/Base/Base.hpp"

#include <Rain/Rain.hpp>

#include <cstdint>
#include <string>
#include <vector>

// Terrain DTOs: the components in .world (the keys terrain2dComponent / terrain3dComponent) and
// the vegetation inside .terrain (the TerrainFloraDto JSON blob).
namespace Panda {

// Grass layer (GrassLayer; the defaults mirror FloraSettings.hpp).
struct GrassLayerDto : public Rain::Codable {
    std::string name = "Grass";
    bool enabled = true;
    uint32_t kind = 0;      // GrassKind (0 Tuft, 1 Mesh)
    uint32_t placement = 0; // GrassPlacement (0 Procedural, 1 Painted, 2 Combined)
    uint32_t maskChannel = 0;
    float density = 1.f;
    float spacing = 0.3f;
    uint32_t soilLayer = 1;
    float minHeight = -1000.f;
    float maxHeight = 1000.f;
    float maxSlopeDeg = 56.6f;
    float noiseCoverage = 1.f;
    float scaleMin = 0.35f;
    float scaleMax = 0.7f;
    float windInfluence = 1.f;
    float brownian = 1.f;
    UUID mesh = 0;
    UUID meshMaterial = 0; // override; empty = the mesh asset's own slot layout
    bool castShadows = false;

    RAIN_FIELDS_BEGIN(GrassLayerDto)
    RAIN_FIELD(name)
    RAIN_FIELD(enabled)
    RAIN_FIELD(kind)
    RAIN_FIELD(placement)
    RAIN_FIELD(maskChannel)
    RAIN_FIELD(density)
    RAIN_FIELD(spacing)
    RAIN_FIELD(soilLayer)
    RAIN_FIELD(minHeight)
    RAIN_FIELD(maxHeight)
    RAIN_FIELD(maxSlopeDeg)
    RAIN_FIELD(noiseCoverage)
    RAIN_FIELD(scaleMin)
    RAIN_FIELD(scaleMax)
    RAIN_FIELD(windInfluence)
    RAIN_FIELD(brownian)
    RAIN_FIELD(mesh)
    RAIN_FIELD(meshMaterial)
    RAIN_FIELD(castShadows)
    RAIN_FIELDS_END
};

// Tree kind (TreeLayer). The tree points are stored in .terrain in binary form, not here.
struct TreeLayerDto : public Rain::Codable {
    uint32_t id = 0;
    std::string name = "Tree";
    bool enabled = true;
    UUID mesh = 0;
    UUID meshMaterial = 0;
    bool imposter = true;
    float canopyDensity = 0.6f;
    float scaleMin = 0.9f;
    float scaleMax = 1.1f;

    RAIN_FIELDS_BEGIN(TreeLayerDto)
    RAIN_FIELD(id)
    RAIN_FIELD(name)
    RAIN_FIELD(enabled)
    RAIN_FIELD(mesh)
    RAIN_FIELD(meshMaterial)
    RAIN_FIELD(imposter)
    RAIN_FIELD(canopyDensity)
    RAIN_FIELD(scaleMin)
    RAIN_FIELD(scaleMax)
    RAIN_FIELDS_END
};

// The terrain's grass palette (GrassGrain): the input of the far-view grain mosaic and the color
// of the Tuft blades.
struct GrassGrainDto : public Rain::Codable {
    Color root = Color(0.07f, 0.2f, 0.05f, 1.f);
    Color tip = Color(0.42f, 0.62f, 0.22f, 1.f);
    Color extra = Color(0.55f, 0.47f, 0.25f, 1.f);
    float extraShare = 0.18f;
    float coverage = 0.8f;
    Color soil = Color(0.36f, 0.5f, 0.26f, 1.f);
    float brightness = 1.f;

    RAIN_FIELDS_BEGIN(GrassGrainDto)
    RAIN_FIELD(root)
    RAIN_FIELD(tip)
    RAIN_FIELD(extra)
    RAIN_FIELD(extraShare)
    RAIN_FIELD(coverage)
    RAIN_FIELD(soil)
    RAIN_FIELD(brightness)
    RAIN_FIELDS_END
};

// Terrain vegetation — a JSON blob inside .terrain.
struct TerrainFloraDto : public Rain::Codable {
    GrassGrainDto grain;
    std::vector<GrassLayerDto> grass;
    std::vector<TreeLayerDto> trees;

    RAIN_FIELDS_BEGIN(TerrainFloraDto)
    RAIN_FIELD(grain)
    RAIN_FIELD(grass)
    RAIN_FIELD(trees)
    RAIN_FIELDS_END
};

struct Terrain2DComponentDto : public Rain::Codable {
    UUID terrain;
    UUID material;
    bool physicsEnabled = true;

    RAIN_FIELDS_BEGIN(Terrain2DComponentDto)
    RAIN_FIELD(terrain)
    RAIN_FIELD(material)
    RAIN_FIELD(physicsEnabled)
    RAIN_FIELDS_END
};

struct Terrain3DComponentDto : public Rain::Codable {
    UUID terrain;
    UUID material;
    bool physicsEnabled = true;

    RAIN_FIELDS_BEGIN(Terrain3DComponentDto)
    RAIN_FIELD(terrain)
    RAIN_FIELD(material)
    RAIN_FIELD(physicsEnabled)
    RAIN_FIELDS_END
};

} // namespace Panda
