#include "Panda/Terrain/TerrainResources.hpp"
#include "Panda/Assets/StaticResources.hpp"
#include "Panda/Extensions/Extensions.hpp"
#include "Panda/GameLogic/GameContext.hpp"
#include "Panda/Terrain/TerrainVertex.hpp"

#include <cmath>

namespace Panda {

namespace Terrain {

    namespace Resources {

        AssetRef<ShaderAsset> shader2D;
        AssetRef<MaterialAsset> material2D;
        AssetRef<ShaderAsset> shader3D;
        AssetRef<MaterialAsset> material3D;
        AssetRef<ShaderAsset> idsShader;
        AssetRef<MaterialAsset> idsMaterial;
        AssetRef<ShaderAsset> shadowDepthShader;
        Miren::VertexLayoutHandle vertexLayout{MIREN_INVALID_HANDLE};
        AssetRef<TextureAsset> noiseTexture;
        AssetRef<ShaderAsset> grassShader;
        AssetRef<MaterialAsset> grassMaterial;

        namespace {

            // Historical slots of the core sequence (StaticResources::initStaticResources
            // reserves them): they cannot change — projects store them in meta.shader.
            const AssetId SHADER_2D_ID = 23;
            const AssetId MATERIAL_2D_ID = 24;
            const AssetId SHADER_3D_ID = 25;
            const AssetId MATERIAL_3D_ID = 26;
            const AssetId IDS_SHADER_ID = 27;
            const AssetId IDS_MATERIAL_ID = 28;
            const AssetId NOISE_TEXTURE_ID = 38;
            const AssetId GRASS_SHADER_ID = 39;
            const AssetId GRASS_MATERIAL_ID = 40;
            const AssetId SHADOW_DEPTH_SHADER_ID = 53;

            // Tiling fbm noise 256² (value noise, lattices 4/8/16… with wrap — the period equals
            // the texture, REPEAT is seamless). The channels are independent patterns: R — grass
            // wind gusts (2 large smooth octaves: small ones jitter when scrolling), G — large
            // color patches, B — spare.
            Miren::TextureCreate makeNoiseTexture() {
                constexpr uint32_t noiseSize = 256;
                const auto latticeHash = [](uint32_t x, uint32_t y, uint32_t seed) {
                    uint32_t h = x * 374761393u + y * 668265263u + seed * 2246822519u;
                    h = (h ^ (h >> 13)) * 1274126177u;
                    return static_cast<float>((h ^ (h >> 16)) & 0xFFFFFFu) / 16777216.f;
                };
                const auto smooth = [](float t) { return t * t * (3.f - 2.f * t); };
                const auto fbm = [&](float fx, float fy, uint32_t seed, int octaves) {
                    float sum = 0.f;
                    float total = 0.f;
                    float amplitude = 1.f;
                    for (int octave = 0; octave < octaves; octave++) {
                        const uint32_t cells = 4u << octave;
                        const float gx = fx * static_cast<float>(cells);
                        const float gy = fy * static_cast<float>(cells);
                        const uint32_t x0 = static_cast<uint32_t>(gx) % cells;
                        const uint32_t y0 = static_cast<uint32_t>(gy) % cells;
                        const uint32_t x1 = (x0 + 1) % cells;
                        const uint32_t y1 = (y0 + 1) % cells;
                        const float tx = smooth(gx - std::floor(gx));
                        const float ty = smooth(gy - std::floor(gy));
                        const uint32_t octaveSeed = seed + static_cast<uint32_t>(octave) * 131u;
                        const float a = latticeHash(x0, y0, octaveSeed);
                        const float b = latticeHash(x1, y0, octaveSeed);
                        const float c = latticeHash(x0, y1, octaveSeed);
                        const float d = latticeHash(x1, y1, octaveSeed);
                        sum += glm::mix(glm::mix(a, b, tx), glm::mix(c, d, tx), ty) * amplitude;
                        total += amplitude;
                        amplitude *= 0.45f;
                    }
                    return sum / total;
                };
                Foundation::Memory noiseData = Foundation::Memory::alloc(noiseSize * noiseSize * 4);
                uint8_t *noisePixels = static_cast<uint8_t *>(noiseData.data);
                for (uint32_t y = 0; y < noiseSize; y++) {
                    for (uint32_t x = 0; x < noiseSize; x++) {
                        const float fx = static_cast<float>(x) / noiseSize;
                        const float fy = static_cast<float>(y) / noiseSize;
                        uint8_t *texel = &noisePixels[(static_cast<size_t>(y) * noiseSize + x) * 4];
                        texel[0] = static_cast<uint8_t>(glm::clamp(fbm(fx, fy, 100u, 2), 0.f, 1.f) * 255.f);
                        texel[1] = static_cast<uint8_t>(glm::clamp(fbm(fx, fy, 200u, 3), 0.f, 1.f) * 255.f);
                        texel[2] = static_cast<uint8_t>(glm::clamp(fbm(fx, fy, 300u, 4), 0.f, 1.f) * 255.f);
                        texel[3] = 255;
                    }
                }
                Miren::TextureCreate create;
                create.m_data = noiseData;
                create.m_width = noiseSize;
                create.m_height = noiseSize;
                create.m_format = Miren::TextureFormat::RGBA8;
                // Mips: the noise is sampled at kilometers too — minification without a mip chain
                // shimmers.
                create.m_minFiltering = Miren::LINEAR_MIPMAP_LINEAR;
                create.m_magFiltering = Miren::LINEAR;
                create.m_wrap = Miren::REPEAT;
                create.m_numMips = 8;
                return create;
            }

            void init(AssetHandler &handler) {
                shader2D = handler.createAsset<ShaderAsset>(
                    SHADER_2D_ID, "terrain-shaders/terrain.vert", "terrain-shaders/terrain.frag"
                );
                // Layer colors: for the 3D terrain they tint the textures of all four layers; for
                // 2D — the far view of layers 2..4 (up close a layer = pure texture), while the far
                // view of layer 1 is the mosaic of the terrain grass palette (the farGrainTexture
                // runtime slot), it has no color of its own.
                const MaterialField layer1Color("layer1Color", MaterialFieldType::COLOR, Vec4(0.36f, 0.5f, 0.26f, 1.f)); // grass
                const MaterialField layer2Color("layer2Color", MaterialFieldType::COLOR, Vec4(0.42f, 0.32f, 0.22f, 1.f)); // dirt
                const MaterialField layer3Color("layer3Color", MaterialFieldType::COLOR, Vec4(0.46f, 0.46f, 0.48f, 1.f)); // rock
                const MaterialField layer4Color("layer4Color", MaterialFieldType::COLOR, Vec4(0.78f, 0.70f, 0.50f, 1.f)); // sand
                // Layer textures (albedo = color × texture); white = pure color, as before.
                const AssetRef<Asset> white = StaticResources::whiteTexture.asBaseAsset();
                const MaterialField layer1Texture("layer1Texture", MaterialFieldType::TEXTURE, white);
                const MaterialField layer2Texture("layer2Texture", MaterialFieldType::TEXTURE, white);
                const MaterialField layer3Texture("layer3Texture", MaterialFieldType::TEXTURE, white);
                const MaterialField layer4Texture("layer4Texture", MaterialFieldType::TEXTURE, white);
                // 2D terrain: + the runtime slots of the far view (fed by Terrain2DAsset::render
                // every frame): fade distances, the asset splatmap, the grass grain mosaic. The
                // defaults are neutral — before the first frame the fade does not kick in, empty
                // textures take the fallbacks by name (bindFields).
                MaterialData material2DData;
                material2DData.inputs = {
                    layer2Color, layer3Color, layer4Color,
                    layer1Texture, layer2Texture, layer3Texture, layer4Texture,
                    MaterialField("farFade", MaterialFieldType::VEC4, Vec4(1.e6f, 2.e6f, 1.f, 0.f), true),
                    MaterialField("splatmap", MaterialFieldType::TEXTURE, AssetRef<Asset>(), true),
                    MaterialField("farGrainTexture", MaterialFieldType::TEXTURE, AssetRef<Asset>(), true),
                };
                material2D = handler.createAsset<MaterialAsset>(MATERIAL_2D_ID, material2DData, shader2D);
                shader3D = handler.createAsset<ShaderAsset>(
                    SHADER_3D_ID, "terrain-shaders/terrain3d.vert", "terrain-shaders/terrain3d.frag"
                );
                MaterialData material3DData;
                material3DData.inputs = {
                    layer1Color, layer2Color, layer3Color, layer4Color,
                    layer1Texture, layer2Texture, layer3Texture, layer4Texture,
                };
                material3D = handler.createAsset<MaterialAsset>(MATERIAL_3D_ID, material3DData, shader3D);
                idsShader = handler.createAsset<ShaderAsset>(
                    IDS_SHADER_ID, "terrain-shaders/ids_terrain.vert", "default-shaders/ids.frag"
                );
                MaterialData idsMaterialData;
                idsMaterialData.inputs = {MaterialField("id", MaterialFieldType::UINT, 0u)};
                idsMaterial = handler.createAsset<MaterialAsset>(IDS_MATERIAL_ID, idsMaterialData, idsShader);
                shadowDepthShader = handler.createAsset<ShaderAsset>(
                    SHADOW_DEPTH_SHADER_ID, "terrain-shaders/shadow_depth_terrain.vert", "default-shaders/shadow_depth.frag"
                );
                // Grass: the noise, the blade shader, the material that carries the fields (the
                // palette and the wind are fed by TerrainFlora from FloraSettings/SceneWind every
                // frame).
                noiseTexture = handler.createAsset<TextureAsset>(NOISE_TEXTURE_ID, makeNoiseTexture());
                grassShader = handler.createAsset<ShaderAsset>(
                    GRASS_SHADER_ID, "terrain-shaders/grass.vert", "terrain-shaders/grass.frag"
                );
                MaterialData grassMaterialData;
                grassMaterialData.inputs = {
                    MaterialField("colorRoot", MaterialFieldType::COLOR, Vec4(0.07f, 0.2f, 0.05f, 1.f)),
                    MaterialField("colorTip", MaterialFieldType::COLOR, Vec4(0.42f, 0.62f, 0.22f, 1.f)),
                    MaterialField("colorExtra", MaterialFieldType::COLOR, Vec4(0.55f, 0.47f, 0.25f, 1.f)),
                    // Service vectors are strictly VEC4: COLOR goes through sRGB→linear on packing
                    // (a power curve), which turns phases/distances into nonsense.
                    MaterialField("windParams", MaterialFieldType::VEC4, Vec4(1.f, 0.f, 0.25f, 1.6f)),
                    MaterialField("timeFade", MaterialFieldType::VEC4, Vec4(0.f, 60.f, 80.f, 0.f)),
                    MaterialField("fadeCamera", MaterialFieldType::VEC4, Vec4(0.f, 0.f, 0.f, 0.f)),
                    MaterialField("doubleSided", MaterialFieldType::UINT, 1u),
                    MaterialField("noiseTexture", MaterialFieldType::TEXTURE, noiseTexture.asBaseAsset())
                };
                grassMaterial = handler.createAsset<MaterialAsset>(GRASS_MATERIAL_ID, grassMaterialData, grassShader);
                // System lighting inputs (the built-in ones have no reflection): lighting, shadow
                // cascades and the occlusion volume — like the lit templates of the core; the depth
                // pass goes without them.
                shader2D->setSystemInputs(true, true, true);
                shader3D->setSystemInputs(true, true, true);
                grassShader->setSystemInputs(true, true, true); // grass receives CSM at the tuft root
                shadowDepthShader->setSystemInputs(false, false);
                vertexLayout = Miren::createVertexLayout(TerrainVertex::createBufferLayoutData());

                Extensions &extensions = GameContext::getExtensions();
                extensions.shaderTemplates.add({shader2D.getId(), "Terrain", material2D});
                extensions.shaderTemplates.add({shader3D.getId(), "Terrain 3D", material3D});
                extensions.meshLayouts.add({vertexLayout, shadowDepthShader, idsMaterial});
            }

            void release() {
                shader2D.reset();
                material2D.reset();
                shader3D.reset();
                material3D.reset();
                idsShader.reset();
                idsMaterial.reset();
                shadowDepthShader.reset();
                noiseTexture.reset();
                grassShader.reset();
                grassMaterial.reset();
                Miren::deleteVertexLayout(vertexLayout);
                vertexLayout = Miren::VertexLayoutHandle{MIREN_INVALID_HANDLE};
            }

        } // namespace

        ResourceProvider provider() {
            ResourceProvider provider;
            provider.name = "terrain";
            provider.init = init;
            provider.release = release;
            return provider;
        }

    } // namespace Resources

} // namespace Terrain

} // namespace Panda
