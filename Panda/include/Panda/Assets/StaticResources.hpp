#pragma once

#include "Panda/Assets/Base/AssetRef.hpp"
#include "Panda/Assets/RuntimeMeshAsset.hpp"
#include "Panda/Assets/MaterialAsset.hpp"

#include <cstdint>

namespace Panda {

enum class PrimitiveMeshKind : uint32_t; // GameLogic/Components/BaseComponents.hpp

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

namespace StaticResources {

    // The built-in assets of the core, then — the shader templates being registered by then —
    // the built-in assets of the modules (Extensions::resourceProviders).
    void initStaticResources();
    void deinitStaticResources();

    extern AssetRef<TextureAsset> defaultSkyTexture;
    extern AssetRef<RuntimeMeshAsset> defaultSkyMesh;
    extern AssetRef<MaterialAsset> defaultSkyMaterial;
    extern AssetRef<TextureAsset> whiteTexture;
    extern AssetRef<TextureAsset> blackTexture;
    // Transparent 1×1 {0,0,0,0}: fallback for premultiplied slots (contributes strictly zero).
    extern AssetRef<TextureAsset> transparentTexture;
    // Instanced variant of the PBR template (lit_instanced vertex shader + pbr fragment shader):
    // flora draws the user's regular PBR materials with it, instanced.
    extern AssetRef<ShaderAsset> instancedPbrShader;
    // Impostor flora: normal baking (cutout for pbr materials / solid for the rest /
    // veg_crown crown — the normal of each leaf) and billboard rendering (quad + instance
    // stream of mat4, the atlases are bound by the renderer).
    extern AssetRef<ShaderAsset> floraImposterNormalShader;
    extern AssetRef<ShaderAsset> floraImposterNormalSolidShader;
    extern AssetRef<ShaderAsset> floraImposterNormalCrownShader;
    extern AssetRef<ShaderAsset> floraBillboardShader;
    // Billboard depth variant for shadow cascades (quad facing the light; billboard shadowVariant).
    extern AssetRef<ShaderAsset> floraBillboardShadowShader;
    extern AssetRef<RuntimeMeshAsset> floraBillboardMesh; // unit quad of the billboard
    // Billboard material: the owner feeds the runtime slots (atlases + parameters) per species.
    extern AssetRef<MaterialAsset> floraBillboardMaterial;
    extern Miren::VertexLayoutHandle floraBillboardVertexLayout; // {corner vec2, uv vec2}
    extern AssetRef<TextureAsset> flatNormalTexture;
    extern AssetRef<ShaderAsset> defaultUnlitShader;
    extern AssetRef<MaterialAsset> defaultUnlitMaterial;
    extern AssetRef<ShaderAsset> defaultLitShader;
    extern AssetRef<MaterialAsset> defaultLitMaterial;
    extern AssetRef<ShaderAsset> defaultPbrShader;
    // Implicit final tonemap (Reinhard+sRGB) — belongs to the pipeline, not shown in the UI.
    extern AssetRef<ShaderAsset> tonemapShader;
    extern AssetRef<MaterialAsset> tonemapMaterial;
    // Built-in bloom pyramid (BloomRenderer) — engine shaders, not shown in the UI.
    // Auto exposure (ExposureRenderer): engine shaders without materials.
    extern AssetRef<ShaderAsset> exposureMeasureShader;
    extern AssetRef<ShaderAsset> exposureReduceShader;
    extern AssetRef<ShaderAsset> exposureAdaptShader;
    extern AssetRef<ShaderAsset> exposureApplyShader;
    extern AssetRef<ShaderAsset> bloomDownsampleShader;
    extern AssetRef<ShaderAsset> bloomUpsampleAddShader;
    extern AssetRef<ShaderAsset> bloomApplyShader;
    extern AssetRef<MaterialAsset> selectedGeometryMaterial;
    extern AssetRef<MaterialAsset> idsMaterial;
    // Greyboxing primitives (unit dimensions; sizes via the transform's scale).
    extern AssetRef<TextureAsset> checkerTexture; // checker pattern for reading level sizes
    extern AssetRef<MaterialAsset> primitiveMaterial;
    extern AssetRef<RuntimeMeshAsset> cubeMesh;
    extern AssetRef<RuntimeMeshAsset> sphereMesh;
    extern AssetRef<RuntimeMeshAsset> capsuleMesh;
    extern AssetRef<RuntimeMeshAsset> planeMesh;
    extern AssetRef<RuntimeMeshAsset> cylinderMesh;
    // Builtin primitive mesh by kind (invalid ref for an unknown one).
    AssetRef<RuntimeMeshAsset> primitiveMeshOf(PrimitiveMeshKind kind);
    // Instancing: a lit shader with the instance matrix in attributes (second vertex stream)
    // and a fallback material for Renderer3D::submitInstanced.
    extern AssetRef<ShaderAsset> instancedLitShader;
    extern AssetRef<MaterialAsset> instancedLitMaterial;
    // Foliage clusters (tree exports): crown tuft cards, leaves — a baked tile of
    // points. The fields template = pbr + crown fields at the end (veg_crown.frag);
    // vegCrownMaterial is the template carrier for choosing the shader in the editor.
    extern AssetRef<ShaderAsset> vegCrownShader;
    extern AssetRef<ShaderAsset> instancedVegCrownShader; // flora draws trees as instances
    extern AssetRef<MaterialAsset> vegCrownMaterial;
    // Vegetation grass: billboard tufts (wind + view fade); the imported "grass"
    // material is switched to this shader manually, the template is vegGrassMaterial.
    extern AssetRef<ShaderAsset> vegGrassShader;
    extern AssetRef<ShaderAsset> instancedVegGrassShader;
    extern AssetRef<MaterialAsset> vegGrassMaterial;
    extern Miren::VertexLayoutHandle generalVertexLayout;
    extern Miren::VertexLayoutHandle instanceMat4Layout;  // 4×vec4 = instance glm::mat4 columns
    // Depth pass of cascade shadows (RenderingMode::SHADOW_DEPTH): selected by vertex layout
    // (general / instances; contributions' own layouts — Extensions::meshLayouts); masked
    // variants cut out the alpha-mask using the caster material's fields (the lit template's
    // naming convention).
    extern AssetRef<ShaderAsset> shadowDepthShader;
    extern AssetRef<ShaderAsset> shadowDepthMaskedShader;
    extern AssetRef<ShaderAsset> shadowDepthInstancedShader;
    extern AssetRef<ShaderAsset> shadowDepthInstancedMaskedShader;
    // Placeholder for the shadowMap slot when there is no shadow atlas (preview, shadows off):
    // depth format (in MSL the slot is depth2d, a color texture is not bound there); the content
    // is not read — the shader exits on shadowParams.w = 0.
    extern AssetRef<TextureAsset> shadowFallbackTexture;
    // Placeholder for the occlusionVolume slot (occlusion volume): 1×1 "open".
    extern AssetRef<TextureAsset> occlusionFallbackTexture;

} // namespace StaticResources

} // namespace Panda
