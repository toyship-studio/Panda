#pragma once

#include "Panda/Assets/Base/AssetRef.hpp"
#include "Panda/Assets/RuntimeMeshAsset.hpp"
#include "Panda/Assets/MaterialAsset.hpp"

namespace Panda {

enum class PrimitiveMeshKind : uint32_t; // GameLogic/Components/BaseComponents.hpp

namespace StaticResources {

    // IDs of static assets are persisted in projects (meta.shader): the sequence is
    // append-only; contribution slots are reserved inside it (see StaticResources.cpp). After the
    // core, the built-in shader templates are registered and the contributions' ResourceProviders
    // are called.
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
    // ACES variant of the implicit final tonemap — for the preview's tonemapOverride
    // (Lab = the production curve).
    extern AssetRef<ShaderAsset> tonemapAcesShader;
    extern AssetRef<MaterialAsset> tonemapAcesMaterial;
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
