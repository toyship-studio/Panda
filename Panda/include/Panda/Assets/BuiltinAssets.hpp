#pragma once

#include <cstdint>

namespace Panda {

// The ids of the built-in assets of the engine core. A project writes them into its files — the
// shader of a material, the material of a mesh — so a number means one thing forever: an asset
// that is gone leaves its number unused, a new asset takes a new number. A module names the ids
// of its own built-in assets the same way, in its own code (ResourceProvider), from the range
// 0xE0000000.. (the Terrain module, the oldest, has 23..28, 38..40 and 53 among these); the asset
// handler refuses a number that is taken.
namespace BuiltinAsset {
    inline constexpr uint32_t SKY_TEXTURE = 1;
    inline constexpr uint32_t SKY_SHADER = 2;
    inline constexpr uint32_t SKY_MATERIAL = 3;
    inline constexpr uint32_t SKY_MESH = 4;
    inline constexpr uint32_t WHITE_TEXTURE = 5;
    inline constexpr uint32_t BLACK_TEXTURE = 6;
    inline constexpr uint32_t FLAT_NORMAL_TEXTURE = 7;
    inline constexpr uint32_t UNLIT_SHADER = 9;
    inline constexpr uint32_t UNLIT_MATERIAL = 10;
    inline constexpr uint32_t LIT_SHADER = 11;
    inline constexpr uint32_t LIT_MATERIAL = 12;
    inline constexpr uint32_t SELECTED_GEOMETRY_SHADER = 13;
    inline constexpr uint32_t SELECTED_GEOMETRY_MATERIAL = 14;
    inline constexpr uint32_t IDS_SHADER = 15;
    inline constexpr uint32_t IDS_MATERIAL = 16;
    inline constexpr uint32_t PBR_SHADER = 17;
    inline constexpr uint32_t TONEMAP_SHADER = 18;
    inline constexpr uint32_t TONEMAP_MATERIAL = 19;
    inline constexpr uint32_t BLOOM_DOWNSAMPLE_SHADER = 20;
    inline constexpr uint32_t BLOOM_UPSAMPLE_ADD_SHADER = 21;
    inline constexpr uint32_t BLOOM_APPLY_SHADER = 22;
    inline constexpr uint32_t CHECKER_TEXTURE = 29;
    inline constexpr uint32_t PRIMITIVE_MATERIAL = 30;
    inline constexpr uint32_t CUBE_MESH = 31;
    inline constexpr uint32_t SPHERE_MESH = 32;
    inline constexpr uint32_t CAPSULE_MESH = 33;
    inline constexpr uint32_t PLANE_MESH = 34;
    inline constexpr uint32_t CYLINDER_MESH = 35;
    inline constexpr uint32_t INSTANCED_LIT_SHADER = 36;
    inline constexpr uint32_t INSTANCED_LIT_MATERIAL = 37;
    inline constexpr uint32_t TRANSPARENT_TEXTURE = 41;
    inline constexpr uint32_t INSTANCED_PBR_SHADER = 42;
    inline constexpr uint32_t FLORA_IMPOSTER_NORMAL_SHADER = 43;
    inline constexpr uint32_t FLORA_IMPOSTER_NORMAL_SOLID_SHADER = 44;
    inline constexpr uint32_t FLORA_BILLBOARD_SHADER = 45;
    inline constexpr uint32_t FLORA_BILLBOARD_MESH = 46;
    inline constexpr uint32_t FLORA_BILLBOARD_MATERIAL = 48;
    inline constexpr uint32_t SHADOW_DEPTH_SHADER = 49;
    inline constexpr uint32_t SHADOW_DEPTH_MASKED_SHADER = 50;
    inline constexpr uint32_t SHADOW_DEPTH_INSTANCED_SHADER = 51;
    inline constexpr uint32_t SHADOW_DEPTH_INSTANCED_MASKED_SHADER = 52;
    inline constexpr uint32_t SHADOW_FALLBACK_TEXTURE = 54;
    inline constexpr uint32_t EXPOSURE_MEASURE_SHADER = 55;
    inline constexpr uint32_t EXPOSURE_REDUCE_SHADER = 56;
    inline constexpr uint32_t EXPOSURE_ADAPT_SHADER = 57;
    inline constexpr uint32_t EXPOSURE_APPLY_SHADER = 58;
    inline constexpr uint32_t VEG_CROWN_SHADER = 59;
    inline constexpr uint32_t VEG_CROWN_MATERIAL = 60;
    inline constexpr uint32_t INSTANCED_VEG_CROWN_SHADER = 61;
    inline constexpr uint32_t VEG_GRASS_SHADER = 62;
    inline constexpr uint32_t VEG_GRASS_MATERIAL = 63;
    inline constexpr uint32_t INSTANCED_VEG_GRASS_SHADER = 64;
    inline constexpr uint32_t FLORA_BILLBOARD_SHADOW_SHADER = 67;
    inline constexpr uint32_t FLORA_IMPOSTER_NORMAL_CROWN_SHADER = 68;
    inline constexpr uint32_t OCCLUSION_FALLBACK_TEXTURE = 69;
} // namespace BuiltinAsset

} // namespace Panda
