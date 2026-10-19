#include "Panda/Renderer/FloraImposter.hpp"

#include "Panda/Assets/Base/AssetHandler.hpp"
#include "Panda/Assets/MaterialSystemValues.hpp"
#include "Panda/Assets/StaticResources.hpp"
#include "Panda/GameLogic/GameContext.hpp"
#include <Foundation/GraphicsAPI.hpp>
#include "Panda/Renderer/MirenViewDistribution.hpp"
#include "Panda/Renderer/Renderer3D.hpp"
#include "Panda/Renderer/ShaderInterface/ShaderBlocks.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Panda {

namespace {

    // The frame of a view angle: an ortho camera from the angle k·(2π/TILES) around Y, the coverage
    // is the rotating AABB (in width — the radius of the XZ diagonal: any view angle fits into the
    // same frame).
    struct BakeFrame {
        glm::mat4 viewProj;
        float halfWidth;
        float halfHeight;
        glm::vec3 toViewer; // unit direction from the mesh axis to the camera of the view angle
    };

    // The coverage radius around the VERTICAL AXIS OF THE MESH (x=0, z=0): the bake and the
    // billboard must share one axis. The billboard centers the quad on the instance origin — if
    // the bake rotates around the AABB center (for an asymmetric crown it is offset from the
    // trunk), the trunk slides off the tile center in its own way in every view angle, and on a
    // tile switch the tree "jumps" sideways.
    float axisRadiusXZ(const glm::vec3 &boundsMin, const glm::vec3 &boundsMax) {
        const float maxAbsX = glm::max(std::abs(boundsMin.x), std::abs(boundsMax.x));
        const float maxAbsZ = glm::max(std::abs(boundsMin.z), std::abs(boundsMax.z));
        return std::sqrt(maxAbsX * maxAbsX + maxAbsZ * maxAbsZ);
    }

    BakeFrame makeBakeFrame(const glm::vec3 &boundsMin, const glm::vec3 &boundsMax, int tile) {
        const glm::vec3 size = boundsMax - boundsMin;
        const float centerY = (boundsMin.y + boundsMax.y) * 0.5f;
        BakeFrame frame;
        frame.halfWidth = axisRadiusXZ(boundsMin, boundsMax) * 1.02f + 0.01f;
        frame.halfHeight = size.y * 0.5f; // vertically the frame = exactly [minY..maxY]
        const float angle =
            static_cast<float>(tile) * glm::two_pi<float>() / FloraImposter::TILES;
        const float distance = frame.halfWidth * 2.f + 1.f;
        const glm::vec3 target(0.f, centerY, 0.f);
        frame.toViewer = glm::vec3(cosf(angle), 0.f, sinf(angle));
        const glm::vec3 eye = target + frame.toViewer * distance;
        const glm::mat4 view = glm::lookAt(eye, target, glm::vec3(0.f, 1.f, 0.f));
        // Precisely orthoZO: NDC depth [0..1]. On Metal the GL variant of ortho ([-1..1]) loses
        // the whole near half of the volume to clipping — the ortho depth is linear, and from the
        // view angles where a part of the mesh is closer than the AABB center, that part was cut
        // out as a whole (the scene perspective is forgiven the same thing: there only
        // [near..2near] goes negative).
        // In GL the depth [0..1] is a valid subregion of the clip volume, it works as is.
        // The frame vertical follows the target convention: GL writes NDC y=+1 into the last
        // row of the texture, Metal/D3D — into row zero; the top/bottom flip aligns
        // the atlas contents between the backends (the billboard UVs are common, v=0 is the top).
        // The winding is inverted by the flip — the bake draws without culling, it is harmless.
        const float bottom = Foundation::ACTIVE_GRAPHICS_TRAITS.originBottomLeft
                                 ? frame.halfHeight
                                 : -frame.halfHeight;
        const glm::mat4 projection = glm::orthoZO(
            -frame.halfWidth, frame.halfWidth, bottom, -bottom, 0.01f,
            distance + frame.halfWidth * 2.f
        );
        frame.viewProj = projection * view;
        return frame;
    }

    Miren::TextureCreate atlasTextureCreate(Miren::TextureFormat format, int tileSize) {
        Miren::TextureCreate create;
        create.m_width = tileSize * FloraImposter::TILES;
        create.m_height = tileSize;
        create.m_format = format;
        // The mips are cut off at level 4: minification in the distance without noise, while the
        // bilinear bleeding of neighboring tiles at the edges stays narrow.
        // The chain is built by generateTextureMipmaps after the last view angle of the bake;
        // the premultiplied color is averaged in the mips correctly by construction.
        create.m_numMips = 4;
        create.m_minFiltering = Miren::LINEAR_MIPMAP_LINEAR;
        create.m_magFiltering = Miren::LINEAR;
        create.m_wrap = Miren::CLAMP;
        return create;
    }

    // The depth targets of the bake have no mips (a trimmed copy of atlasTextureCreate).
    Miren::TextureCreate atlasDepthCreate(int tileSize) {
        Miren::TextureCreate create;
        create.m_width = tileSize * FloraImposter::TILES;
        create.m_height = tileSize;
        create.m_format = Miren::TextureFormat::DEPTH24STENCIL8;
        create.m_wrap = Miren::CLAMP;
        return create;
    }

    // The primitive's material by the rules of a mesh layer (override → per-primitive → default lit).
    AssetRef<MaterialAsset> resolvePrimitiveMaterial(
        const AssetRef<MaterialAsset> &overrideMaterial,
        const std::vector<AssetRef<Asset>> &primitiveMaterials,
        size_t flatIndex
    ) {
        AssetRef<MaterialAsset> material = overrideMaterial;
        if (!material && flatIndex < primitiveMaterials.size()) {
            material = primitiveMaterials[flatIndex].as<MaterialAsset>();
        }
        if (!material.isValid()) { material = StaticResources::defaultLitMaterial; }
        return material;
    }

} // namespace

FloraImposter::~FloraImposter() {
    for (auto &entry : m_entries) { releaseBakeResources(*entry); }
    // The atlases are assets: the cascade cleanup frees them when the refs of the entries die.
}

FloraImposter::View FloraImposter::ensure(
    AssetRef<ImportedMeshAsset> mesh, AssetRef<MaterialAsset> overrideMaterial
) {
    View view;
    ImportedMeshAsset *meshAsset = mesh.get();
    if (meshAsset == nullptr) { return view; }

    const AssetId meshId = mesh.getId();
    const AssetId overrideId =
        overrideMaterial.isValid() ? overrideMaterial.getId() : AssetId(0);
    // The layout belongs to the mesh itself; its snapshot is part of the cache key: a slot change
    // = a new atlas.
    std::vector<AssetId> materialIds;
    materialIds.reserve(meshAsset->getPrimitiveMaterials().size());
    for (const AssetRef<Asset> &material : meshAsset->getPrimitiveMaterials()) {
        materialIds.push_back(material.getId());
    }

    Entry *entry = nullptr;
    for (auto &candidate : m_entries) {
        if (candidate->meshId == meshId && candidate->overrideId == overrideId &&
            candidate->materialIds == materialIds) {
            entry = candidate.get();
            break;
        }
    }
    if (entry == nullptr) {
        auto created = std::make_unique<Entry>();
        created->meshId = meshId;
        created->overrideId = overrideId;
        created->materialIds = std::move(materialIds);
        created->mesh = mesh;
        created->overrideMaterial = overrideMaterial;
        // The atlas frame must match the billboard quad and the pivot baked into the
        // instance matrices — the bounds of the same mesh that built the instances.
        if (!meshAsset->getMeshBounds(created->boundsMin, created->boundsMax)) {
            created->boundsMin = glm::vec3(-0.5f, 0.f, -0.5f);
            created->boundsMax = glm::vec3(0.5f, 1.f, 0.5f);
        }
        // The atlas is in the HDR format of the scene: the color is baked in linear light
        // (ambient=1 → albedo).
        AssetHandler *handler = GameContext::getAssetHandler();
        created->colorAtlas = handler->createRuntimeAsset<TextureAsset>(
            atlasTextureCreate(Miren::TextureFormat::RGBA16F, COLOR_TILE_SIZE)
        );
        created->normalAtlas = handler->createRuntimeAsset<TextureAsset>(
            atlasTextureCreate(Miren::TextureFormat::RGBA8, NORMAL_TILE_SIZE)
        );
        created->colorDepth = Miren::createTexture(atlasDepthCreate(COLOR_TILE_SIZE));
        created->normalDepth = Miren::createTexture(atlasDepthCreate(NORMAL_TILE_SIZE));
        Miren::FrameBufferAttachment colorAttachments[] = {
            created->colorAtlas->getMirenHandle(), created->colorDepth
        };
        Miren::FrameBufferSpecification colorSpec(colorAttachments, 2);
        created->colorFb = Miren::createFrameBuffer(colorSpec);
        Miren::FrameBufferAttachment normalAttachments[] = {
            created->normalAtlas->getMirenHandle(), created->normalDepth
        };
        Miren::FrameBufferSpecification normalSpec(normalAttachments, 2);
        created->normalFb = Miren::createFrameBuffer(normalSpec);
        // The crown surface comes from the vegCrown material (the override or a mesh slot):
        // the billboard repeats the Cook-Torrance of the mesh crown (surfaceParams of the billboard).
        auto readCrownSurface = [&](AssetRef<MaterialAsset> material) {
            if (!material.isValid() || material.get() == nullptr) { return false; }
            if (material->getShaderAsset()->getMirenHandle().id !=
                StaticResources::vegCrownShader->getMirenHandle().id) {
                return false;
            }
            created->crownMetallic = material->getFloatFieldValue("metallicFactor", 0.f);
            created->crownRoughness = material->getFloatFieldValue("roughnessFactor", 1.f);
            created->crownTranslucency = material->getFloatFieldValue("translucency", 0.f);
            return true;
        };
        if (!readCrownSurface(overrideMaterial)) {
            for (const AssetRef<Asset> &candidate : meshAsset->getPrimitiveMaterials()) {
                if (readCrownSurface(candidate.as<MaterialAsset>())) { break; }
            }
        }
        m_entries.push_back(std::move(created));
        entry = m_entries.back().get();
    }

    view.colorAtlas = entry->colorAtlas;
    view.normalAtlas = entry->normalAtlas;
    const glm::vec3 size = entry->boundsMax - entry->boundsMin;
    view.width = axisRadiusXZ(entry->boundsMin, entry->boundsMax) * 2.02f + 0.02f;
    view.height = size.y;
    view.minY = entry->boundsMin.y;
    view.crownMetallic = entry->crownMetallic;
    view.crownRoughness = entry->crownRoughness;
    view.crownTranslucency = entry->crownTranslucency;
    view.ready = entry->bakedTiles >= TILES;
    return view;
}

void FloraImposter::processBaking() {
    for (auto &entry : m_entries) {
        if (entry->bakedTiles >= TILES) { continue; }
        if (!entry->mesh.isValid()) { continue; } // the mesh is unloaded — wait
        bakeTile(*entry, entry->bakedTiles);
        entry->bakedTiles++;
        if (entry->bakedTiles >= TILES) {
            releaseBakeResources(*entry);
            // The mip chains of the atlases are built from the finished mip 0: the command is
            // executed after this frame is rendered (the last view angle is already in the atlas),
            // the billboards start being drawn from the next one (ready from this moment).
            Miren::generateTextureMipmaps(entry->colorAtlas->getMirenHandle());
            Miren::generateTextureMipmaps(entry->normalAtlas->getMirenHandle());
        }
        return; // one view angle per frame
    }
}

void FloraImposter::bakeTile(Entry &entry, int tile) {
    const BakeFrame frame = makeBakeFrame(entry.boundsMin, entry.boundsMax, tile);
    const Miren::Rect colorRect(
        tile * COLOR_TILE_SIZE, 0, COLOR_TILE_SIZE, COLOR_TILE_SIZE
    );
    const Miren::Rect normalRect(
        tile * NORMAL_TILE_SIZE, 0, NORMAL_TILE_SIZE, NORMAL_TILE_SIZE
    );

    // The first view angle of the pass clears the whole atlas (Metal: a clear = the loadAction of
    // the whole target); the subsequent ones draw their tiles on top.
    Miren::setViewFrameBuffer(Views::FLORA_IMPOSTER_COLOR_VIEW, entry.colorFb);
    Miren::setViewport(Views::FLORA_IMPOSTER_COLOR_VIEW, colorRect);
    Miren::setViewClear(Views::FLORA_IMPOSTER_COLOR_VIEW, 0x00000000);
    Miren::setViewClearEnabled(Views::FLORA_IMPOSTER_COLOR_VIEW, tile == 0);
    Miren::setViewFrameBuffer(Views::FLORA_IMPOSTER_NORMAL_VIEW, entry.normalFb);
    Miren::setViewport(Views::FLORA_IMPOSTER_NORMAL_VIEW, normalRect);
    Miren::setViewClear(Views::FLORA_IMPOSTER_NORMAL_VIEW, 0x00000000);
    Miren::setViewClearEnabled(Views::FLORA_IMPOSTER_NORMAL_VIEW, tile == 0);

    // Bake lighting: pure ambient — the pbr/lit fragment shader returns ~albedo (the light is
    // applied by the billboard shader using the baked normal; otherwise the lighting would be
    // doubled).
    WorldLightingData bakeLighting;
    bakeLighting.ambientColor = Color(1.f, 1.f, 1.f, 1.f);
    bakeLighting.ambientIntensity = 1.f;
    bakeLighting.directionalLight = {};
    bakeLighting.pointLightCount = 0;
    const SceneLightsUbo lightingUbo = SceneLightsUbo::from(bakeLighting);
    const VertexUbo vertUbo{frame.viewProj, glm::mat4(1.f)};

    const ImportedMeshAsset *meshAsset = entry.mesh.get();
    size_t primitiveFlat = 0;
    for (size_t submeshIndex = 0; submeshIndex < meshAsset->submeshCount(); submeshIndex++) {
        const ImportedSubmesh &submesh = meshAsset->submeshAt(submeshIndex);
        for (const ImportedPrimitive &primitive : submesh.primitives) {
            const size_t flatIndex = primitiveFlat++;
            if (!primitive.vertexBufferHandle.isValid() ||
                !primitive.indexBufferHandle.isValid()) {
                continue;
            }
            AssetRef<MaterialAsset> material = resolvePrimitiveMaterial(
                entry.overrideMaterial, meshAsset->getPrimitiveMaterials(), flatIndex
            );

            // The view fade of the foliage is muted for the duration of the bake: the dither of the
            // edge-on cards would bake holes into the atlas forever — each of the 8 ortho view
            // angles catches its own "edges", and the billboard systematically lost foliage. The
            // wind is muted for the same reason: the atlas would bake in a random phase forever —
            // the drift of the cards and the tilt of the leaves at the moment of the bake.
            const float savedViewFade = material->getFloatFieldValue("viewFade", 0.f);
            if (savedViewFade != 0.f) { material->setFieldValue("viewFade", 0.f); }
            // The wind strength is a SYSTEM field (the value is set by SceneWind, editing the
            // material field is powerless): the registry itself is muted for the two submits of
            // the bake.
            const MaterialFieldValue *systemWind = MaterialSystemValues::find("windStrength");
            const float savedWind =
                systemWind && std::holds_alternative<float>(*systemWind)
                    ? std::get<float>(*systemWind)
                    : 0.f;
            if (savedWind != 0.f) { MaterialSystemValues::set("windStrength", 0.f); }

            // Color: the primitive's material as is (textures/cutoff/fields), ambient light.
            Miren::ProgramHandle colorShader = material->getShaderAsset()->getMirenHandle();
            Miren::setState(MIREN_STATE_DEPTH_TEST); // no culling — the foliage is double-sided
            material->bindFields();
            Miren::addInputUniformBuffer("SCENE_LIGHTS", &lightingUbo, sizeof(lightingUbo));
            // The slots of the system light textures get placeholders (a bake without shadows).
            Renderer3D::bindSystemLightTextures(material->getShaderAsset(), nullptr, nullptr);
            Miren::addInputUniformBuffer("UBO_VERT", &vertUbo, sizeof(vertUbo));
            Miren::setVertexBuffer(primitive.vertexBufferHandle);
            Miren::setIndexBuffer(primitive.indexBufferHandle, 0, primitive.indicesCount);
            Miren::submit(Views::FLORA_IMPOSTER_COLOR_VIEW, colorShader);

            // Normals. A veg_crown crown has its own pass: the normal of EVERY leaf (the normal
            // tile through the card basis, the plane faces the camera of the view angle) — the same
            // one the mesh is lit with in the scene; the pbr pass wrote the vertex normal (the crown
            // ellipsoid), and the billboard in the distance was a smooth ball next to the variegated
            // foliage. The other PBR templates — a cutout by the layout fields (veg_grass: the
            // prefix = pbr, the texture alpha cuts the silhouette like MASK); the rest — solid.
            // The template is determined by the Miren program: the template asset id of the
            // materials generated by the import does not match StaticResources, the program is
            // the same.
            const bool crownTemplate =
                colorShader.id == StaticResources::vegCrownShader->getMirenHandle().id;
            const bool grassTemplate =
                colorShader.id == StaticResources::vegGrassShader->getMirenHandle().id;
            const bool pbrTemplate =
                grassTemplate ||
                colorShader.id == StaticResources::defaultPbrShader->getMirenHandle().id ||
                colorShader.id == StaticResources::instancedPbrShader->getMirenHandle().id;
            AssetRef<ShaderAsset> normalShaderAsset =
                crownTemplate ? StaticResources::floraImposterNormalCrownShader
                : pbrTemplate ? StaticResources::floraImposterNormalShader
                              : StaticResources::floraImposterNormalSolidShader;
            Miren::ProgramHandle normalShader = normalShaderAsset->getMirenHandle();
            Miren::setState(MIREN_STATE_DEPTH_TEST);
            // The back-face flip in the pbr pass is the default_pbr convention (it flips in the
            // scene as well). The grass does NOT flip the normal (the volume normal of the field —
            // the decision is made), while its cards are double-sided and half of them face the
            // bake camera of the view angle with the back side: with the flip the normals went
            // into the atlas inverted — the billboard was darker than the mesh.
            // doubleSided is muted for the pass (it is the only one that reads the field — the
            // flip gets disabled).
            const uint32_t savedDoubleSided = material->getUIntFieldValue("doubleSided", 0u);
            const bool suppressDoubleSided = grassTemplate && savedDoubleSided != 0u;
            if (suppressDoubleSided) { material->setFieldValue("doubleSided", 0u); }
            if (crownTemplate || pbrTemplate) { material->bindFields(); }
            if (crownTemplate) {
                // The direction to the camera of the view angle is one per tile (an ortho camera):
                // the card plane turns towards the viewer, as towards the camera in the scene.
                const float toViewer[4] = {
                    frame.toViewer.x, frame.toViewer.y, frame.toViewer.z, 0.f
                };
                Miren::addInputUniformBuffer("IMPOSTER_BAKE", toViewer, sizeof(toViewer));
            }
            Miren::addInputUniformBuffer("UBO_VERT", &vertUbo, sizeof(vertUbo));
            Miren::setVertexBuffer(primitive.vertexBufferHandle);
            Miren::setIndexBuffer(primitive.indexBufferHandle, 0, primitive.indicesCount);
            Miren::submit(Views::FLORA_IMPOSTER_NORMAL_VIEW, normalShader);

            if (suppressDoubleSided) { material->setFieldValue("doubleSided", savedDoubleSided); }
            if (savedViewFade != 0.f) { material->setFieldValue("viewFade", savedViewFade); }
            if (savedWind != 0.f) { MaterialSystemValues::set("windStrength", savedWind); }
        }
    }
}

void FloraImposter::releaseBakeResources(Entry &entry) {
    if (entry.colorFb.isValid()) { Miren::deleteFrameBuffer(entry.colorFb); }
    if (entry.normalFb.isValid()) { Miren::deleteFrameBuffer(entry.normalFb); }
    if (entry.colorDepth.isValid()) { Miren::deleteTexture(entry.colorDepth); }
    if (entry.normalDepth.isValid()) { Miren::deleteTexture(entry.normalDepth); }
    entry.colorFb = Miren::FrameBufferHandle();
    entry.normalFb = Miren::FrameBufferHandle();
    entry.colorDepth = Miren::TextureHandle();
    entry.normalDepth = Miren::TextureHandle();
}

} // namespace Panda
