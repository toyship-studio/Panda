#pragma once

#include "Panda/Assets/Base/AssetRef.hpp"
#include "Panda/Base/Base.hpp"
#include "Panda/Base/ColorSpace.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace Panda {

// Terrain vegetation — two lists of different nature:
//   grass and bushes (GrassLayer) — a FILL: a layer fills an area by rules and/or by
//     the brush mask, the plants are scattered deterministically and are not stored anywhere;
//   trees (TreeLayer) — POINTS: each tree stands at its own coordinate (TerrainTrees),
//     the layer describes only the tree kind.

// Kind of a grass layer.
enum class GrassKind : uint32_t {
    Tuft = 0, // built-in blades: their own vertex format, terrain wind, color — the terrain palette
    Mesh = 1, // an authored mesh as mat4 instances: exported grass tufts, flowers, bushes
};

// Placement of a grass layer: rules (filters of soil/height/slope/noise patches), the brush
// (a channel of the terrain's painted mask — the artist's direct intent) or their combination —
// a procedural base on top of which the brush edits with priority (the channel stores an offset
// from the neutral 128: above — add plants, below — erase, including the procedural ones). The
// slope is always respected, in any mode.
enum class GrassPlacement : uint32_t {
    Procedural = 0,
    Painted = 1,
    Combined = 2, // Procedural + Brush
};

struct GrassLayer {
    std::string name = "Grass";
    bool enabled = true;
    GrassKind kind = GrassKind::Tuft;
    GrassPlacement placement = GrassPlacement::Procedural;
    uint32_t maskChannel = 0;  // Painted/Combined: channel of the terrain's RGBA mask (0..3)
    float density = 1.f;       // Tuft: fraction of the maximum density (0..1)
    // Mesh: scattering step in meters (the probability at a point is given by the weight of
    // soil/patches/brush — at full weight one plant per spacing²).
    float spacing = 0.3f;
    uint32_t soilLayer = 1;    // Procedural: 0 = any ground, 1..4 = weight of a splatmap layer
    float minHeight = -1000.f; // Procedural: relief height filter (terrain local space, m)
    float maxHeight = 1000.f;
    float maxSlopeDeg = 56.6f; // steeper — does not grow
    float noiseCoverage = 1.f; // Procedural: 1 = solid, less — value-noise patches
    float scaleMin = 0.35f;    // Tuft: plant height (m); Mesh: size multiplier
    float scaleMax = 0.7f;
    float windInfluence = 1.f; // Tuft: 0 = does not bend, 1 = full response to the scene wind
    float brownian = 1.f;      // Tuft: strength of the chaos of the tuft shape
    // Mesh: the imported mesh as a whole. The pivot is the bottom of the mesh AABB (the plant
    // "stands" on the ground).
    AssetRef<Asset> mesh; // ImportedMeshAsset
    // Override material: set — the whole mesh uses it; empty — the mesh asset's own slot layout.
    AssetRef<Asset> meshMaterial;
    bool castShadows = false; // Mesh: bushes cast a shadow, grass does not
};

// Tree kind. The positions are TerrainTrees points with a reference to the layer id.
struct TreeLayer {
    // Stable identifier: points reference it (TreeInstance::layer), so the order of the layers
    // in the list may be changed. Issued by the editor (max + 1), never 0.
    uint32_t id = 0;
    std::string name = "Tree";
    bool enabled = true;
    AssetRef<Asset> mesh;         // ImportedMeshAsset
    AssetRef<Asset> meshMaterial; // override; empty = the slot layout of the mesh asset
    // Beyond the tree range (FloraDistances::tree) the tree continues as a billboard with
    // a baked atlas up to the cap of the imposter zone; disabled — it disappears at the range.
    bool imposter = true;
    // Crown density for the occlusion volume (Occlusion Volume): 0 — does not occlude
    // (a boulder); spruce ~0.9, birch ~0.5.
    float canopyDensity = 0.6f;
    // Scale spread of NEW points (brush, scattering); standing trees have their own scale.
    float scaleMin = 0.9f;
    float scaleMax = 1.1f;
};

// The terrain's grass palette. The ONLY input of the far-view grain mosaic
// (TerrainGrainTexture) and the color of the built-in Tuft blades: the distance and the near
// grass are of the same hue. Nothing from outside — neither the grass layers nor the terrain
// material — affects the mosaic.
// The colors are a hue at a comfortable picker brightness; the meadow brightness is brightness
// (an albedo multiplier in linear light, like baseColorFactor of a material): a dark meadow is
// set by it, not by black colors, at which the picker no longer holds the hue.
struct GrassGrain {
    Color root = Color(0.07f, 0.2f, 0.05f, 1.f);
    Color tip = Color(0.42f, 0.62f, 0.22f, 1.f);
    Color extra = Color(0.55f, 0.47f, 0.25f, 1.f); // "dry" grains
    Color soil = Color(0.36f, 0.5f, 0.26f, 1.f);   // soil between the grains
    float extraShare = 0.18f; // share of grains of the extra color (the rest — the root→tip gradient)
    float coverage = 0.8f;    // share of mosaic texels occupied by grains; the remainder — soil
    float brightness = 1.f;   // albedo multiplier of the whole palette (0..1)

    // A palette color with brightness: picker sRGB → linear × brightness → sRGB (the consumers —
    // the mosaic and the COLOR fields of the Tuft material — decode sRGB themselves).
    Color shaded(const Color &color) const {
        const Color linear = srgbToLinear(color);
        return linearToSrgb(Color(linear.r * brightness, linear.g * brightness, linear.b * brightness, color.a));
    }
};

// Terrain vegetation (a part of .terrain). The wind is the scene wind (SceneWind).
struct FloraSettings {
    GrassGrain grain;
    std::vector<GrassLayer> grass;
    std::vector<TreeLayer> trees;
};

} // namespace Panda
