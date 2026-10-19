#include "Panda/Assets/StaticResources.hpp"
#include "Panda/Extensions/Extensions.hpp"
#include "Panda/GameLogic/GameContext.hpp"
#include "Panda/Renderer/Vertex.hpp"
#include "Panda/Renderer/PrimitiveMeshes.hpp"
#include "Panda/GameLogic/Components/BaseComponents.hpp"

namespace Panda {

namespace StaticResources {

    struct SkyVertex {
        glm::vec3 pos;

        SkyVertex(float x, float y, float z)
            : pos(x, y, z) {}
    };

    void initStaticResources() {
        // Static resource IDs are PERSISTED: materials (.mat) store the id of a built-in shader in
        // meta.shader. The createAsset order must not be changed, new resources are added only AT
        // THE END.
        UUID ASSET_ID = 1;
        AssetHandler *handler = GameContext::getAssetHandler();
        /// ----------------------------------------
        /// SKY MESH
        /// ----------------------------------------
        // The built-in environment map — a placeholder for the system slot environmentMap until
        // the world sets its own (Settings → World → Environment Map): an equirect gradient
        // zenith → horizon → ground, baked in code (there is no file in the resources). The id slot
        // is the same one the former sky texture had — the createAsset order does not change.
        {
            constexpr uint32_t SKY_WIDTH = 64;
            constexpr uint32_t SKY_HEIGHT = 32;
            const glm::vec3 zenith(0.20f, 0.42f, 0.78f);
            const glm::vec3 horizon(0.74f, 0.83f, 0.92f);
            const glm::vec3 ground(0.34f, 0.32f, 0.30f);
            Foundation::Memory skyPixels = Foundation::Memory::alloc(SKY_WIDTH * SKY_HEIGHT * 4);
            uint8_t *pixel = static_cast<uint8_t *>(skyPixels.data);
            for (uint32_t row = 0; row < SKY_HEIGHT; row++) {
                // The top of the image is the zenith (v = 0.5 − asin(y)/π).
                const float elevation = 0.5f - (static_cast<float>(row) + 0.5f) / SKY_HEIGHT; // −0.5..0.5 of π
                const float t = glm::clamp(std::abs(elevation) * 2.f, 0.f, 1.f);
                const glm::vec3 color = elevation >= 0.f ? glm::mix(horizon, zenith, std::sqrt(t))
                                                         : glm::mix(horizon * 0.8f, ground, std::sqrt(t));
                for (uint32_t column = 0; column < SKY_WIDTH; column++) {
                    *pixel++ = static_cast<uint8_t>(color.r * 255.f);
                    *pixel++ = static_cast<uint8_t>(color.g * 255.f);
                    *pixel++ = static_cast<uint8_t>(color.b * 255.f);
                    *pixel++ = 255;
                }
            }
            Miren::TextureCreate skyTextureConfig;
            skyTextureConfig.m_data = skyPixels;
            skyTextureConfig.m_width = SKY_WIDTH;
            skyTextureConfig.m_height = SKY_HEIGHT;
            skyTextureConfig.m_format = Miren::TextureFormat::RGBA8_SRGB; // authored colors (sRGB)
            skyTextureConfig.m_minFiltering = Miren::TextureFiltering::LINEAR;
            skyTextureConfig.m_magFiltering = Miren::TextureFiltering::LINEAR;
            skyTextureConfig.m_wrap = Miren::TextureWrapMode::REPEAT;
            defaultSkyTexture = handler->createAsset<TextureAsset>(ASSET_ID++, skyTextureConfig);
        }
        AssetRef<ShaderAsset> defaultSkyShader = handler->createAsset<ShaderAsset>(
            ASSET_ID++, "default-shaders/sky/sky_hdr.vert", "default-shaders/sky/sky_hdr.frag"
        );
        // The default sky draws the world's ENVIRONMENT MAP (the system texture environmentMap,
        // the exposure is the tail of SCENE_LIGHTS); the material has no fields. There is no sun on
        // purpose: the disc lives in the project's custom sky material (sky_gradient).
        defaultSkyMaterial = handler->createAsset<MaterialAsset>(ASSET_ID++, MaterialData{}, defaultSkyShader);
        defaultSkyShader->setSystemInputs(true, false, false, true);
        SkyVertex vertices[24] = {
            // Front
            SkyVertex(-1.0f, -1.0f, 1.0f), // 0
            SkyVertex(1.0f, -1.0f, 1.0f),  // 1
            SkyVertex(1.0f, 1.0f, 1.0f),   // 2
            SkyVertex(-1.0f, 1.0f, 1.0f),  // 3
            // Back
            SkyVertex(-1.0f, -1.0f, -1.0f), // 4
            SkyVertex(-1.0f, 1.0f, -1.0f),  // 5
            SkyVertex(1.0f, 1.0f, -1.0f),   // 6
            SkyVertex(1.0f, -1.0f, -1.0f),  // 7
            // Top
            SkyVertex(-1.0f, 1.0f, -1.0f), // 8
            SkyVertex(-1.0f, 1.0f, 1.0f),  // 11
            SkyVertex(1.0f, 1.0f, 1.0f),   // 10
            SkyVertex(1.0f, 1.0f, -1.0f),  // 9
            // Bottom
            SkyVertex(-1.0f, -1.0f, -1.0f), // 12
            SkyVertex(1.0f, -1.0f, -1.0f),  // 13
            SkyVertex(1.0f, -1.0f, 1.0f),   // 14
            SkyVertex(-1.0f, -1.0f, 1.0f),  // 15
            // Left
            SkyVertex(-1.0f, -1.0f, -1.0f), // 16
            SkyVertex(-1.0f, -1.0f, 1.0f),  // 17
            SkyVertex(-1.0f, 1.0f, 1.0f),   // 18
            SkyVertex(-1.0f, 1.0f, -1.0f),  // 19
            // Right
            SkyVertex(1.0f, -1.0f, -1.0f), // 20
            SkyVertex(1.0f, 1.0f, -1.0f),  // 23
            SkyVertex(1.0f, 1.0f, 1.0f),   // 22
            SkyVertex(1.0f, -1.0f, 1.0f)   // 21
        };
        // clang-format off
        uint32_t indices[36] = {
            0, 2, 1, 2, 0, 3,       // Front
            4, 6, 5, 6, 4, 7,       // Back
            8, 10, 9, 10, 8, 11,    // Top
            12, 14, 13, 14, 12, 15, // Bottom
            16, 18, 17, 18, 16, 19, // Left
            20, 22, 21, 22, 20, 23  // Right
        };
        // clang-format on
        Miren::VertexBufferLayoutData skyLayoutData;
        skyLayoutData.pushVec3();
        Miren::VertexLayoutHandle skyLayoutHandle = Miren::createVertexLayout(skyLayoutData);
        Foundation::Memory verticesMemory = Foundation::Memory::copying(vertices, sizeof(SkyVertex) * 24);
        Foundation::Memory indicesMemory = Foundation::Memory::copying(indices, sizeof(uint32_t) * 36);
        MeshData skyMeshData(skyLayoutHandle, verticesMemory, 24 * sizeof(SkyVertex), indicesMemory, 36);
        defaultSkyMesh = handler->createAsset<RuntimeMeshAsset>(ASSET_ID++);
        defaultSkyMesh->update(skyMeshData);
        /// ----------------------------------------
        /// WHITE TEXTURE
        /// ----------------------------------------
        Foundation::Memory whiteTextureData = Foundation::Memory::alloc(sizeof(uint32_t));
        *(uint32_t *)whiteTextureData.data = 0xffffffff;
        Miren::TextureCreate whiteTextureCreate;
        whiteTextureCreate.m_data = whiteTextureData;
        whiteTexture = handler->createAsset<TextureAsset>(ASSET_ID++, whiteTextureCreate);
        Foundation::Memory blackTextureData = Foundation::Memory::alloc(sizeof(uint32_t));
        *(uint32_t *)blackTextureData.data = 0xFF000000;
        Miren::TextureCreate blackTextureCreate;
        blackTextureCreate.m_data = blackTextureData;
        blackTexture = handler->createAsset<TextureAsset>(ASSET_ID++, blackTextureCreate);
        Foundation::Memory flatNormalTextureData = Foundation::Memory::alloc(sizeof(uint32_t));
        *(uint32_t *)flatNormalTextureData.data = 0xFFFF8080;
        Miren::TextureCreate flatNormalTextureCreate;
        flatNormalTextureCreate.m_data = flatNormalTextureData;
        flatNormalTexture = handler->createAsset<TextureAsset>(ASSET_ID++, flatNormalTextureCreate);
        // Retired demo-logo slot: subsequent builtin IDs are persisted in project assets.
        ASSET_ID++;
        /// ----------------------------------------
        /// DEFAULT UNLIT SHADER
        /// ----------------------------------------
        defaultUnlitShader = handler->createAsset<ShaderAsset>(
            ASSET_ID++, "default-shaders/default_unlit.vert", "default-shaders/default_unlit.frag"
        );
        /// ----------------------------------------
        /// DEFAULT UNLIT MATERIAL
        /// ----------------------------------------
        MaterialData materialData;
        materialData.inputs = {MaterialField("baseColorTexture", MaterialFieldType::TEXTURE, whiteTexture.asBaseAsset())
        };
        defaultUnlitMaterial = handler->createAsset<MaterialAsset>(ASSET_ID++, materialData, defaultUnlitShader);
        /// ----------------------------------------
        /// DEFAULT LIT SHADER
        /// ----------------------------------------
        defaultLitShader = handler->createAsset<ShaderAsset>(
            ASSET_ID++, "default-shaders/default_lit.vert", "default-shaders/default_lit.frag"
        );
        /// ----------------------------------------
        /// DEFAULT LIT MATERIAL
        /// ----------------------------------------
        MaterialData litMaterialData;
        litMaterialData.inputs = {
            MaterialField("baseColorTexture", MaterialFieldType::TEXTURE, whiteTexture.asBaseAsset()),
            MaterialField("baseColorFactor", MaterialFieldType::COLOR, Vec4(1.f, 1.f, 1.f, 1.f)),
            MaterialField("metallicRoughnessTexture", MaterialFieldType::TEXTURE, whiteTexture.asBaseAsset()),
            MaterialField("metallicFactor", MaterialFieldType::FLOAT, 0.f),
            MaterialField("roughnessFactor", MaterialFieldType::FLOAT, 1.f),
            MaterialField("normalTexture", MaterialFieldType::TEXTURE, flatNormalTexture.asBaseAsset()),
            MaterialField("normalScale", MaterialFieldType::FLOAT, 1.f),
            MaterialField("occlusionTexture", MaterialFieldType::TEXTURE, whiteTexture.asBaseAsset()),
            MaterialField("occlusionStrength", MaterialFieldType::FLOAT, 1.f),
            MaterialField("emissiveTexture", MaterialFieldType::TEXTURE, AssetRef<Asset>()),
            MaterialField("emissiveFactor", MaterialFieldType::COLOR, Vec4(0.f, 0.f, 0.f, 1.f)),
            MaterialField("alphaMode", MaterialFieldType::UINT, 0u),
            MaterialField("alphaCutoff", MaterialFieldType::FLOAT, 0.5f),
            MaterialField("doubleSided", MaterialFieldType::UINT, 0u),
            // New fields go only AT THE END: MATERIAL_FIELDS is packed in field order (std140).
            MaterialField("emissiveIntensity", MaterialFieldType::FLOAT, 1.f)
        };
        defaultLitMaterial = handler->createAsset<MaterialAsset>(ASSET_ID++, litMaterialData, defaultLitShader);
        /// ----------------------------------------
        /// SELECTED GEOMETRY SHADER
        /// ----------------------------------------
        AssetRef<ShaderAsset> selectedGeometryShader = handler->createAsset<ShaderAsset>(
            ASSET_ID++, "default-shaders/selection_map.vert", "default-shaders/selection_map.frag"
        );
        /// ----------------------------------------
        /// SELECTED GEOMETRY MATERIAL
        /// ----------------------------------------
        selectedGeometryMaterial =
            handler->createAsset<MaterialAsset>(ASSET_ID++, materialData, selectedGeometryShader);
        /// ----------------------------------------
        /// IDS SHADER
        /// ----------------------------------------
        AssetRef<ShaderAsset> idsShader =
            handler->createAsset<ShaderAsset>(ASSET_ID++, "default-shaders/ids.vert", "default-shaders/ids.frag");
        /// ----------------------------------------
        /// IDS MATERIAL
        /// ----------------------------------------
        materialData.inputs = {MaterialField("id", MaterialFieldType::UINT, 0u)};
        idsMaterial = handler->createAsset<MaterialAsset>(ASSET_ID++, materialData, idsShader);
        /// ----------------------------------------
        /// DEFAULT PBR SHADER (metallic-roughness, the vertex shader is shared with lit)
        /// ----------------------------------------
        defaultPbrShader = handler->createAsset<ShaderAsset>(
            ASSET_ID++, "default-shaders/default_lit.vert", "default-shaders/default_pbr.frag"
        );
        /// ----------------------------------------
        /// POSTPROCESSING (engine pipeline shaders; not shown in the UI)
        /// ----------------------------------------
        // The bloom building-block assets (id 18..25) are removed: a legacy meta.shader pointing to
        // these ids resolves to the Lit fallback (a type guard in the material loaders).
        // The implicit final HDR→screen pass (executed by the pipeline; do NOT add it to the shader
        // combo box).
        tonemapShader = handler->createAsset<ShaderAsset>(
            ASSET_ID++, "default-shaders/postprocess.vert", "default-shaders/tonemap.frag"
        );
        MaterialData emptyPassData;
        tonemapMaterial = handler->createAsset<MaterialAsset>(ASSET_ID++, emptyPassData, tonemapShader);
        // The pyramid of the built-in bloom (BloomRenderer): engine shaders without materials, the
        // uniforms are bound directly; they are not shown to the user.
        bloomDownsampleShader = handler->createAsset<ShaderAsset>(
            ASSET_ID++, "default-shaders/postprocess.vert", "default-shaders/bloom_downsample.frag"
        );
        bloomUpsampleAddShader = handler->createAsset<ShaderAsset>(
            ASSET_ID++, "default-shaders/postprocess.vert", "default-shaders/bloom_upsample_add.frag"
        );
        bloomApplyShader = handler->createAsset<ShaderAsset>(
            ASSET_ID++, "default-shaders/postprocess.vert", "default-shaders/bloom_apply.frag"
        );
        // Slots 23..28 are reserved for a contribution (Terrain::Resources: the 2D/3D shaders and
        // materials, the IDS variant): the ids are persisted in projects, the contribution creates
        // the assets itself.
        ASSET_ID = UUID(ASSET_ID + 6);
        /// ----------------------------------------
        /// VERTEX LAYOUTS
        /// ----------------------------------------
        generalVertexLayout = Miren::createVertexLayout(Vertex::createBufferLayoutData());
        /// ----------------------------------------
        /// GREYBOXING PRIMITIVES (checker + meshes; only AT THE END — the ASSET_ID order)
        /// ----------------------------------------
        {
            // Procedural checker 256²: a 64px cell (4×4 per texture → 25 cm on a 1 m face),
            // two shades of gray; sRGB + a full mip pyramid (minification without shimmer).
            constexpr uint32_t checkerSize = 256;
            constexpr uint32_t cellPixels = 64;
            Foundation::Memory checkerData =
                Foundation::Memory::alloc(checkerSize * checkerSize * sizeof(uint32_t));
            uint32_t *pixels = static_cast<uint32_t *>(checkerData.data);
            for (uint32_t y = 0; y < checkerSize; y++) {
                for (uint32_t x = 0; x < checkerSize; x++) {
                    const bool light = ((x / cellPixels) + (y / cellPixels)) % 2 == 0;
                    pixels[y * checkerSize + x] = light ? 0xFFB4B4B4 : 0xFF787878;
                }
            }
            Miren::TextureCreate checkerCreate;
            checkerCreate.m_data = checkerData;
            checkerCreate.m_width = checkerSize;
            checkerCreate.m_height = checkerSize;
            checkerCreate.m_format = Miren::TextureFormat::RGBA8_SRGB;
            checkerCreate.m_minFiltering = Miren::LINEAR_MIPMAP_LINEAR;
            checkerCreate.m_magFiltering = Miren::LINEAR;
            checkerCreate.m_wrap = Miren::REPEAT;
            checkerCreate.m_numMips = 8;
            checkerTexture = handler->createAsset<TextureAsset>(ASSET_ID++, checkerCreate);

            MaterialData primitiveMaterialData;
            primitiveMaterialData.inputs = litMaterialData.inputs;
            for (MaterialField &field : primitiveMaterialData.inputs) {
                if (field.name == "baseColorTexture") { field.value = checkerTexture.asBaseAsset(); }
            }
            primitiveMaterial =
                handler->createAsset<MaterialAsset>(ASSET_ID++, primitiveMaterialData, defaultLitShader);

            cubeMesh = handler->createAsset<RuntimeMeshAsset>(ASSET_ID++);
            cubeMesh->update(PrimitiveMeshes::buildCube());
            sphereMesh = handler->createAsset<RuntimeMeshAsset>(ASSET_ID++);
            sphereMesh->update(PrimitiveMeshes::buildSphere());
            capsuleMesh = handler->createAsset<RuntimeMeshAsset>(ASSET_ID++);
            capsuleMesh->update(PrimitiveMeshes::buildCapsule());
            planeMesh = handler->createAsset<RuntimeMeshAsset>(ASSET_ID++);
            planeMesh->update(PrimitiveMeshes::buildPlane());
            cylinderMesh = handler->createAsset<RuntimeMeshAsset>(ASSET_ID++);
            cylinderMesh->update(PrimitiveMeshes::buildCylinder());
        }
        /// ----------------------------------------
        /// INSTANCING (only AT THE END — the ASSET_ID order)
        /// ----------------------------------------
        {
            instancedLitShader = handler->createAsset<ShaderAsset>(
                ASSET_ID++, "default-shaders/default_lit_instanced.vert", "default-shaders/default_lit.frag"
            );
            MaterialData instancedLitMaterialData;
            instancedLitMaterialData.inputs = litMaterialData.inputs;
            instancedLitMaterial =
                handler->createAsset<MaterialAsset>(ASSET_ID++, instancedLitMaterialData, instancedLitShader);
            // Instance stream: columns of the instance transform matrix (glm::mat4, column-major).
            Miren::VertexBufferLayoutData instanceLayoutData;
            instanceLayoutData.pushVec4();
            instanceLayoutData.pushVec4();
            instanceLayoutData.pushVec4();
            instanceLayoutData.pushVec4();
            instanceMat4Layout = Miren::createVertexLayout(instanceLayoutData);
        }
        /// ----------------------------------------
        /// TERRAIN MODULE SLOTS (only AT THE END — the ASSET_ID order)
        /// ----------------------------------------
        {
            // 38..40 — the noise, the shader and the material of the terrain grass
            // (Terrain::Resources): projects store their ids; the resources are created by the
            // module, the core skips the slots.
            ASSET_ID = UUID(ASSET_ID + 3);
            Foundation::Memory transparentTextureData = Foundation::Memory::alloc(sizeof(uint32_t));
            *(uint32_t *)transparentTextureData.data = 0x00000000;
            Miren::TextureCreate transparentTextureCreate;
            transparentTextureCreate.m_data = transparentTextureData;
            transparentTexture =
                handler->createAsset<TextureAsset>(ASSET_ID++, transparentTextureCreate);

            // Instanced variant of the PBR template: the same fragment shader, a vertex shader with
            // instance attributes (PBR and lit share the vertex shader — they share the instanced
            // version as well). The flora renderer uses it to draw the user's PBR materials
            // (generated by the import) without manual reconfiguration.
            instancedPbrShader = handler->createAsset<ShaderAsset>(
                ASSET_ID++, "default-shaders/default_lit_instanced.vert",
                "default-shaders/default_pbr.frag"
            );

            // Impostor flora: normal baking (the shared lit vertex shader; the crown pass is at
            // the end of the list) + the billboard.
            floraImposterNormalShader = handler->createAsset<ShaderAsset>(
                ASSET_ID++, "default-shaders/default_lit.vert",
                "default-shaders/flora_imposter_normal.frag"
            );
            floraImposterNormalSolidShader = handler->createAsset<ShaderAsset>(
                ASSET_ID++, "default-shaders/default_lit.vert",
                "default-shaders/flora_imposter_normal_solid.frag"
            );
            floraBillboardShader = handler->createAsset<ShaderAsset>(
                ASSET_ID++, "default-shaders/flora_billboard.vert",
                "default-shaders/flora_billboard.frag"
            );
            Miren::VertexBufferLayoutData billboardLayoutData;
            billboardLayoutData.pushVec2(); // corner
            billboardLayoutData.pushVec2(); // uv (v=0 — the top of the atlas)
            floraBillboardVertexLayout = Miren::createVertexLayout(billboardLayoutData);
            // Billboard quad: x ∈ [-0.5..0.5], y ∈ [0..1] (the bottom is on the ground).
            const float quadVertices[] = {
                -0.5f, 0.f, 0.f, 1.f, //
                0.5f,  0.f, 1.f, 1.f, //
                0.5f,  1.f, 1.f, 0.f, //
                -0.5f, 1.f, 0.f, 0.f, //
            };
            const uint32_t quadIndices[] = {0, 1, 2, 0, 2, 3};
            MeshData quadMesh(
                floraBillboardVertexLayout,
                Foundation::Memory::copying(quadVertices, sizeof(quadVertices)),
                sizeof(quadVertices),
                Foundation::Memory::copying(quadIndices, sizeof(quadIndices)), 6
            );
            quadMesh.vertexStride = sizeof(float) * 4;
            floraBillboardMesh = handler->createAsset<RuntimeMeshAsset>(ASSET_ID++);
            floraBillboardMesh->update(quadMesh);

            // Retired preview-blit slot: the program is now owned by PandaEditor.
            ASSET_ID++;

            // The material of the impostor flora billboards (a quad + mat4 instances, the generic
            // submitInstanced): the runtime slots are fed by the owner per species — the bake
            // atlases and the frame parameters; doubleSided — the quad always faces the camera.
            MaterialData billboardMaterialData;
            billboardMaterialData.inputs = {
                MaterialField("sizeParams", MaterialFieldType::VEC4, Vec4(1.f, 1.f, 0.f, 8.f), true),
                MaterialField("cameraLocal", MaterialFieldType::VEC4, Vec4(0.f, 0.f, 0.f, 0.f), true),
                MaterialField("fadeParams", MaterialFieldType::VEC4, Vec4(1.e6f, 2.e6f, 0.f, 0.f), true),
                MaterialField("doubleSided", MaterialFieldType::UINT, 1u),
                // The crown surface of the species (x — metallic, y — roughness): the billboard's
                // Cook-Torrance = that of the mesh crown. The depth shaders read the PREFIX of the
                // blob — the field goes strictly after doubleSided, before the textures.
                MaterialField("surfaceParams", MaterialFieldType::VEC4, Vec4(0.f, 1.f, 0.f, 0.f), true),
                MaterialField("imposterColorTexture", MaterialFieldType::TEXTURE, AssetRef<Asset>(), true),
                MaterialField("imposterNormalTexture", MaterialFieldType::TEXTURE, AssetRef<Asset>(), true),
            };
            floraBillboardMaterial = handler->createAsset<MaterialAsset>(
                ASSET_ID++, billboardMaterialData, floraBillboardShader
            );
        }
        /// ----------------------------------------
        /// SHADOW DEPTH (only AT THE END — the ASSET_ID order)
        /// ----------------------------------------
        {
            shadowDepthShader = handler->createAsset<ShaderAsset>(
                ASSET_ID++, "default-shaders/shadow_depth.vert", "default-shaders/shadow_depth.frag"
            );
            shadowDepthMaskedShader = handler->createAsset<ShaderAsset>(
                ASSET_ID++, "default-shaders/shadow_depth.vert", "default-shaders/shadow_depth_masked.frag"
            );
            shadowDepthInstancedShader = handler->createAsset<ShaderAsset>(
                ASSET_ID++, "default-shaders/shadow_depth_instanced.vert", "default-shaders/shadow_depth.frag"
            );
            shadowDepthInstancedMaskedShader = handler->createAsset<ShaderAsset>(
                ASSET_ID++, "default-shaders/shadow_depth_instanced.vert", "default-shaders/shadow_depth_masked.frag"
            );
            ASSET_ID++; // slot 53 is reserved for a contribution (Terrain::Resources: depth pass)
            Miren::TextureCreate shadowFallbackCreate;
            shadowFallbackCreate.m_format = Miren::TextureFormat::DEPTH32F;
            shadowFallbackCreate.m_width = 4;
            shadowFallbackCreate.m_height = 4;
            shadowFallbackCreate.m_wrap = Miren::CLAMP_TO_BORDER;
            shadowFallbackCreate.m_compare = Miren::COMPARE_LESS_EQUAL;
            shadowFallbackTexture = handler->createAsset<TextureAsset>(ASSET_ID++, shadowFallbackCreate);
        }
        // Auto exposure (ExposureRenderer): measure → reduce → adapt → apply.
        // STRICTLY AT THE END of the ASSET_ID sequence: user assets persist
        // references to builtin ids (meta.shader in .mat.pmeta, the materials of imported
        // trees) — an insertion in the middle shifted the ids and broke them (the terrain and
        // the leaves disappeared: their materials already pointed to foreign shaders).
        exposureMeasureShader = handler->createAsset<ShaderAsset>(
            ASSET_ID++, "default-shaders/postprocess.vert", "default-shaders/exposure_measure.frag"
        );
        exposureReduceShader = handler->createAsset<ShaderAsset>(
            ASSET_ID++, "default-shaders/postprocess.vert", "default-shaders/exposure_reduce.frag"
        );
        exposureAdaptShader = handler->createAsset<ShaderAsset>(
            ASSET_ID++, "default-shaders/postprocess.vert", "default-shaders/exposure_adapt.frag"
        );
        exposureApplyShader = handler->createAsset<ShaderAsset>(
            ASSET_ID++, "default-shaders/postprocess.vert", "default-shaders/exposure_apply.frag"
        );
        // Foliage clusters (tree exports): the cluster cards of the crown, the leaves are a baked
        // tile of points. A vertex shader of its own — the cards sway in the wind (with the same
        // gust wave as the flutter/flashes of the fragment shader). Also at the end — the rule
        // above.
        vegCrownShader = handler->createAsset<ShaderAsset>(
            ASSET_ID++, "default-shaders/veg_crown.vert", "default-shaders/veg_crown.frag"
        );
        // The carrier of the veg_crown field template (the pbr set + the crown parameters):
        // choosing the shader in the editor synchronizes the material fields with this set AND its
        // values. The crown defaults go on top of the lit set — the material is valid right after
        // the switch: double-sided, a MASK cutout by the tile mask, a working gloss roughness
        // (the lit default of 1.0 kills the foliage specular).
        MaterialData vegCrownMaterialData;
        vegCrownMaterialData.inputs = litMaterialData.inputs;
        vegCrownMaterialData.getField("doubleSided").value = 1u;
        vegCrownMaterialData.getField("alphaMode").value = 1u;
        vegCrownMaterialData.getField("roughnessFactor").value = 0.25f;
        vegCrownMaterialData.inputs.push_back(
            MaterialField("viewFade", MaterialFieldType::FLOAT, 0.25f)
        );
        // Foliage wind (per-leaf oscillation of the normals + card sway).
        // The names are SYSTEM ones: the values are set by the engine (SceneWind — the world wind),
        // the field defaults apply only until a system value is published.
        vegCrownMaterialData.inputs.push_back(
            MaterialField("windStrength", MaterialFieldType::FLOAT, 0.4f)
        );
        vegCrownMaterialData.inputs.push_back(
            MaterialField("windGustiness", MaterialFieldType::FLOAT, 0.6f)
        );
        // The leaf lighting basis: the card plane (0) ↔ the crown volume normal (1) —
        // the crown vertex normal stores the volume (an ellipsoid), the fraction is tuned live.
        vegCrownMaterialData.inputs.push_back(
            MaterialField("canopyNormal", MaterialFieldType::FLOAT, 0.6f)
        );
        // Leaf translucency: the fraction of the sun passing through the blade when the sun is
        // behind the leaf relative to the camera (a silhouette against the sun, thin spots); it is
        // attenuated by the shadow and the volume.
        vegCrownMaterialData.inputs.push_back(
            MaterialField("translucency", MaterialFieldType::FLOAT, 0.35f)
        );
        // The crown normal tile lives in the STANDARD normalTexture slot of the lit set (rgb —
        // the leaf/needle normal, a — the leaf random; baked by ShootBaker) — there is no separate
        // field, the slot default is a flat normal.
        vegCrownMaterial =
            handler->createAsset<MaterialAsset>(ASSET_ID++, vegCrownMaterialData, vegCrownShader);
        // Instanced variant of the crown: the flora (Mesh layers) draws trees as instances —
        // Renderer3D swaps the shader for the variant of the same template (the fragment shader is
        // shared).
        instancedVegCrownShader = handler->createAsset<ShaderAsset>(
            ASSET_ID++, "default-shaders/veg_crown_instanced.vert", "default-shaders/veg_crown.frag"
        );
        // Vegetation grass (tufts of intersecting billboards): cutout of the user
        // texture + vertex wind + view fade. The template = the lit set with MASK and
        // doubleSided + the grass fields at the end; the user switches the "grass" material from
        // the import to this shader manually (like the crown). Also at the end — the rule above.
        vegGrassShader = handler->createAsset<ShaderAsset>(
            ASSET_ID++, "default-shaders/veg_grass.vert", "default-shaders/veg_grass.frag"
        );
        MaterialData vegGrassMaterialData;
        vegGrassMaterialData.inputs = litMaterialData.inputs;
        vegGrassMaterialData.getField("alphaMode").value = 1u;
        vegGrassMaterialData.getField("doubleSided").value = 1u;
        vegGrassMaterialData.inputs.push_back(
            MaterialField("viewFade", MaterialFieldType::FLOAT, 0.25f)
        );
        // Wind — system names (the values are set by the engine, SceneWind), as for the crown.
        vegGrassMaterialData.inputs.push_back(
            MaterialField("windStrength", MaterialFieldType::FLOAT, 0.4f)
        );
        vegGrassMaterialData.inputs.push_back(
            MaterialField("windGustiness", MaterialFieldType::FLOAT, 0.6f)
        );
        // The root→tip tuft gradient is a MATERIAL one (by the height fraction COLOR_0.a): Lab and
        // the editor turn the same controls, a dark root gives volume without rebaking the
        // vertices.
        vegGrassMaterialData.inputs.push_back(
            MaterialField("rootColor", MaterialFieldType::COLOR, Vec4(0.20f, 0.28f, 0.10f, 1.f))
        );
        vegGrassMaterialData.inputs.push_back(
            MaterialField("tipColor", MaterialFieldType::COLOR, Vec4(0.45f, 0.55f, 0.22f, 1.f))
        );
        // Dissolve distances of the mesh grass: the default is "do not fade out" (previews/ordinary
        // worlds); the flora sets them as a runtime field on the GrassMesh layers.
        vegGrassMaterialData.inputs.push_back(
            MaterialField("grassFade", MaterialFieldType::VEC4, Vec4(1.e6f, 2.e6f, 0.f, 0.f))
        );
        vegGrassMaterial =
            handler->createAsset<MaterialAsset>(ASSET_ID++, vegGrassMaterialData, vegGrassShader);
        instancedVegGrassShader = handler->createAsset<ShaderAsset>(
            ASSET_ID++, "default-shaders/veg_grass_instanced.vert", "default-shaders/veg_grass.frag"
        );
        // System light inputs of the builtin shaders (there is no reflection — they are declared
        // explicitly). The instanced variants share the fragment shader. The veg_grass grass
        // receives CSM at the tuft root (like the tuft grass), the veg_crown foliage — at the
        // fragment point (naive self-shadowing by the crown — deliberate), the impostor
        // billboards — per fragment as well (inside Shadow Distance they catch the shadows of the
        // mesh zone; they are not casters themselves — there is no acne).
        for (AssetRef<ShaderAsset> shader :
             {defaultLitShader,
              defaultPbrShader,
              instancedLitShader,
              instancedPbrShader,
              vegGrassShader,
              instancedVegGrassShader,
              vegCrownShader,
              instancedVegCrownShader,
              floraBillboardShader}) {
            shader->setSystemInputs(true, true, true);
        }
        // Environment map reflections — for PBR (the instanced variant shares the fragment shader).
        for (AssetRef<ShaderAsset> shader : {defaultPbrShader, instancedPbrShader}) {
            shader->setSystemInputs(true, true, true, true);
        }
        // Instanced variants are a property of the template shaders: the instanced draw of
        // Renderer3D asks the shader itself for the variant and knows no specific templates.
        defaultLitShader->setInstancedVariant(instancedLitShader.asBaseAsset());
        defaultPbrShader->setInstancedVariant(instancedPbrShader.asBaseAsset());
        vegCrownShader->setInstancedVariant(instancedVegCrownShader.asBaseAsset());
        vegGrassShader->setInstancedVariant(instancedVegGrassShader.asBaseAsset());
        // ACES variant of the implicit final pass (tonemapOverride): the Lab preview is finalized
        // with the same curve as the user ACES chains of the worlds. At the end — the id rule.
        tonemapAcesShader = handler->createAsset<ShaderAsset>(
            ASSET_ID++, "default-shaders/postprocess.vert", "default-shaders/tonemap_aces.frag"
        );
        tonemapAcesMaterial =
            handler->createAsset<MaterialAsset>(ASSET_ID++, MaterialData{}, tonemapAcesShader);
        // Depth variant of the impostor billboard (the forest shadow beyond Flora Distance): the
        // quad turns towards the light in its own vertex shader — the builtin shadow_depth does not
        // fit. At the end — the id rule.
        floraBillboardShadowShader = handler->createAsset<ShaderAsset>(
            ASSET_ID++, "default-shaders/flora_billboard_shadow.vert",
            "default-shaders/flora_billboard_shadow.frag"
        );
        floraBillboardShader->setShadowVariant(floraBillboardShadowShader.asBaseAsset());
        for (AssetRef<ShaderAsset> shader :
             {shadowDepthShader,
              shadowDepthMaskedShader,
              shadowDepthInstancedShader,
              shadowDepthInstancedMaskedShader,
              // SCENE_LIGHTS (the light direction) is bound for the depth variant of the billboard
              // by bindShadowDepthProgram by hand: the system mechanism is silent in the cascades,
              // and the shadow atlas at this moment is a render target, not an input.
              floraBillboardShadowShader}) {
            shader->setSystemInputs(false, false);
        }
        // The normal pass of the impostor bake for the veg_crown crown: the crown vertex shader
        // (the wind is muted by the bake) + the normal of every leaf from the normal tile — the
        // billboard repeats the foliage of the mesh (FloraImposter::bakeTile). At the end — the id
        // rule.
        floraImposterNormalCrownShader = handler->createAsset<ShaderAsset>(
            ASSET_ID++, "default-shaders/veg_crown.vert",
            "default-shaders/flora_imposter_normal_crown.frag"
        );
        // Placeholder for the occlusionVolume slot: 1×1 "sky visibility 1, transmittance 1" — there
        // is no occlusion (previews, flora without crowns). AT THE END of the ids — the rule above:
        // an insertion in the middle shifts the ids of all built-in assets below, and the project
        // materials refer to foreign shaders and textures.
        {
            Miren::TextureCreate occlusionFallbackCreate;
            occlusionFallbackCreate.m_format = Miren::TextureFormat::RGBA8;
            occlusionFallbackCreate.m_width = 1;
            occlusionFallbackCreate.m_height = 1;
            occlusionFallbackCreate.m_depth = 1; // volume: the shaders declare occlusionVolume as Texture3D
            occlusionFallbackCreate.m_minFiltering = Miren::LINEAR;
            occlusionFallbackCreate.m_magFiltering = Miren::LINEAR;
            occlusionFallbackCreate.m_wrap = Miren::CLAMP;
            const uint8_t occlusionFallbackPixel[4] = {255, 255, 255, 255};
            occlusionFallbackCreate.m_data = Foundation::Memory::copying(occlusionFallbackPixel, 4);
            occlusionFallbackTexture = handler->createAsset<TextureAsset>(ASSET_ID++, occlusionFallbackCreate);
        }
        /// ----------------------------------------
        /// REGISTRIES: the built-in shader templates (the editor picker, reconciliation of .mat
        /// fields) and the static resources of contributions — after the core: they need its
        /// placeholder textures.
        /// ----------------------------------------
        Extensions &extensions = GameContext::getExtensions();
        extensions.shaderTemplates.add({defaultUnlitShader.getId(), "Unlit", defaultUnlitMaterial});
        extensions.shaderTemplates.add({defaultLitShader.getId(), "Lit", defaultLitMaterial});
        extensions.shaderTemplates.add({defaultPbrShader.getId(), "PBR", defaultLitMaterial}); // the same field set
        extensions.shaderTemplates.add({vegCrownShader.getId(), "Vegetation Crown", vegCrownMaterial});
        extensions.shaderTemplates.add({vegGrassShader.getId(), "Vegetation Grass", vegGrassMaterial});
        for (const ResourceProvider &provider : extensions.resourceProviders.providers()) {
            provider.init(*handler);
        }
        extensions.resourceProviders.setLive(handler); // late providers (project plugins) — right away
    }

    AssetRef<RuntimeMeshAsset> primitiveMeshOf(PrimitiveMeshKind kind) {
        switch (kind) {
            case PrimitiveMeshKind::Cube: return cubeMesh;
            case PrimitiveMeshKind::Sphere: return sphereMesh;
            case PrimitiveMeshKind::Capsule: return capsuleMesh;
            case PrimitiveMeshKind::Plane: return planeMesh;
            case PrimitiveMeshKind::Cylinder: return cylinderMesh;
        }
        return {};
    }

    void deinitStaticResources() {
        Extensions &extensions = GameContext::getExtensions();
        extensions.resourceProviders.setLive(nullptr);
        for (const ResourceProvider &provider : extensions.resourceProviders.providers()) {
            provider.release();
        }
        extensions.shaderTemplates.clear(); // template carriers and layout programs are the assets above
        extensions.meshLayouts.clear();
        shadowFallbackTexture = {};
        occlusionFallbackTexture = {};
        shadowDepthShader = {};
        shadowDepthMaskedShader = {};
        shadowDepthInstancedShader = {};
        shadowDepthInstancedMaskedShader = {};
        vegCrownShader = {};
        instancedVegCrownShader = {};
        vegGrassShader = {};
        instancedVegGrassShader = {};
        vegGrassMaterial = {};
        vegCrownMaterial = {};
        defaultSkyTexture.reset();
        defaultSkyMesh.reset();
        defaultSkyMaterial.reset();
        whiteTexture.reset();
        blackTexture.reset();
        flatNormalTexture.reset();
        defaultUnlitShader.reset();
        defaultUnlitMaterial.reset();
        defaultLitShader.reset();
        defaultLitMaterial.reset();
        defaultPbrShader.reset();
        tonemapShader.reset();
        tonemapMaterial.reset();
        tonemapAcesShader.reset();
        tonemapAcesMaterial.reset();
        exposureMeasureShader.reset();
        exposureReduceShader.reset();
        exposureAdaptShader.reset();
        exposureApplyShader.reset();
        bloomDownsampleShader.reset();
        bloomUpsampleAddShader.reset();
        bloomApplyShader.reset();
        checkerTexture.reset();
        primitiveMaterial.reset();
        cubeMesh.reset();
        sphereMesh.reset();
        capsuleMesh.reset();
        planeMesh.reset();
        cylinderMesh.reset();
        instancedLitShader.reset();
        instancedLitMaterial.reset();
        transparentTexture.reset();
        instancedPbrShader.reset();
        floraImposterNormalShader.reset();
        floraImposterNormalSolidShader.reset();
        floraImposterNormalCrownShader.reset();
        floraBillboardShader.reset();
        floraBillboardShadowShader.reset();
        floraBillboardMesh.reset();
        floraBillboardMaterial.reset();
        selectedGeometryMaterial.reset();
        idsMaterial.reset();
        Miren::deleteVertexLayout(generalVertexLayout);
        Miren::deleteVertexLayout(instanceMat4Layout);
        Miren::deleteVertexLayout(floraBillboardVertexLayout);
    }

    AssetRef<TextureAsset> defaultSkyTexture;
    AssetRef<RuntimeMeshAsset> defaultSkyMesh;
    AssetRef<MaterialAsset> defaultSkyMaterial;
    AssetRef<TextureAsset> whiteTexture;
    AssetRef<TextureAsset> blackTexture;
    AssetRef<TextureAsset> flatNormalTexture;
    AssetRef<ShaderAsset> defaultUnlitShader;
    AssetRef<MaterialAsset> defaultUnlitMaterial;
    AssetRef<ShaderAsset> defaultLitShader;
    AssetRef<MaterialAsset> defaultLitMaterial;
    AssetRef<ShaderAsset> defaultPbrShader;
    AssetRef<ShaderAsset> tonemapShader;
    AssetRef<MaterialAsset> tonemapMaterial;
    AssetRef<ShaderAsset> tonemapAcesShader;
    AssetRef<MaterialAsset> tonemapAcesMaterial;
    AssetRef<ShaderAsset> exposureMeasureShader;
    AssetRef<ShaderAsset> exposureReduceShader;
    AssetRef<ShaderAsset> exposureAdaptShader;
    AssetRef<ShaderAsset> exposureApplyShader;
    AssetRef<ShaderAsset> bloomDownsampleShader;
    AssetRef<ShaderAsset> bloomUpsampleAddShader;
    AssetRef<ShaderAsset> bloomApplyShader;
    AssetRef<MaterialAsset> selectedGeometryMaterial;
    AssetRef<MaterialAsset> idsMaterial;
    AssetRef<TextureAsset> checkerTexture;
    AssetRef<MaterialAsset> primitiveMaterial;
    AssetRef<RuntimeMeshAsset> cubeMesh;
    AssetRef<RuntimeMeshAsset> sphereMesh;
    AssetRef<RuntimeMeshAsset> capsuleMesh;
    AssetRef<RuntimeMeshAsset> planeMesh;
    AssetRef<RuntimeMeshAsset> cylinderMesh;
    AssetRef<ShaderAsset> instancedLitShader;
    AssetRef<MaterialAsset> instancedLitMaterial;
    AssetRef<TextureAsset> transparentTexture;
    AssetRef<ShaderAsset> instancedPbrShader;
    AssetRef<ShaderAsset> floraImposterNormalShader;
    AssetRef<ShaderAsset> floraImposterNormalSolidShader;
    AssetRef<ShaderAsset> floraImposterNormalCrownShader;
    AssetRef<ShaderAsset> floraBillboardShader;
    AssetRef<ShaderAsset> floraBillboardShadowShader;
    AssetRef<RuntimeMeshAsset> floraBillboardMesh;
    AssetRef<MaterialAsset> floraBillboardMaterial;
    AssetRef<ShaderAsset> vegCrownShader;
    AssetRef<ShaderAsset> instancedVegCrownShader;
    AssetRef<MaterialAsset> vegCrownMaterial;
    AssetRef<ShaderAsset> vegGrassShader;
    AssetRef<ShaderAsset> instancedVegGrassShader;
    AssetRef<MaterialAsset> vegGrassMaterial;
    Miren::VertexLayoutHandle floraBillboardVertexLayout{MIREN_INVALID_HANDLE};
    Miren::VertexLayoutHandle generalVertexLayout;
    Miren::VertexLayoutHandle instanceMat4Layout;
    AssetRef<ShaderAsset> shadowDepthShader;
    AssetRef<ShaderAsset> shadowDepthMaskedShader;
    AssetRef<ShaderAsset> shadowDepthInstancedShader;
    AssetRef<ShaderAsset> shadowDepthInstancedMaskedShader;
    AssetRef<TextureAsset> shadowFallbackTexture;
    AssetRef<TextureAsset> occlusionFallbackTexture;

} // namespace StaticResources

} // namespace Panda
