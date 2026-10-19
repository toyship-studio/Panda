#pragma once

#include "Panda/Assets/ImportedMeshAsset.hpp"
#include "Panda/Assets/MaterialAsset.hpp"
#include "Panda/Assets/TextureAsset.hpp"

#include <Miren/Miren.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace Panda {

// Automatic baking of impostor atlases for flora meshes: the plant as seen from TILES view
// angles around the vertical axis, into two atlases — color (premultiplied: rgb·coverage,
// α = silhouette) and world normals (for a veg_crown crown — the normal of EVERY leaf in its
// own pass: the billboard is lit by the same foliage and the same Cook-Torrance as the mesh).
// Mips (levels 0..4) are built by generateTextureMipmaps after the last view angle —
// noise-free minification in the distance. One view angle is baked per frame into its own
// views (before the scene), the cache is keyed by the mesh and the material set; changing the
// layer's materials gives a new entry. No hand-made images: the atlas always matches the mesh.
class FloraImposter {
public:
    static constexpr int TILES = 8; // view angles around the Y axis
    // Both atlases are 512: the per-leaf color variegation (a ~20 cm leaf = 4–6 texels on a
    // 10–20 m crown) and the normal of EVERY leaf survive the bake only from 512 up — at 256
    // they averaged into a single tone and a flat normal. Memory is ~27 MB per species
    // (4096×512 atlases + mips), the bake is one-time, the runtime reads the mip by screen
    // density — no more expensive for distant billboards.
    static constexpr int COLOR_TILE_SIZE = 512;
    static constexpr int NORMAL_TILE_SIZE = 512;

    // A ready (or still baking) atlas for billboard rendering. The atlases are system
    // TextureAsset (stable ids): the billboard material takes them as ordinary TEXTURE fields.
    struct View {
        AssetRef<TextureAsset> colorAtlas;
        AssetRef<TextureAsset> normalAtlas;
        float width = 1.f;  // width of the view angle's frame in meters (covers the mesh rotation)
        float height = 1.f; // frame height = height of the mesh AABB
        float minY = 0.f;   // AABB bottom (the billboard base)
        // Crown surface of the species (metallic/roughness of the vegCrown material): the
        // billboard is lit by the same Cook-Torrance as the mesh crown — without them the
        // far zone was matte and responded to the sun differently.
        float crownMetallic = 0.f;
        float crownRoughness = 1.f;
        float crownTranslucency = 0.f; // leaf translucency (surfaceParams.z of the billboard)
        bool ready = false; // false — still baking (do not draw billboards)
    };

    FloraImposter() = default;
    ~FloraImposter();
    FloraImposter(const FloraImposter &) = delete;
    FloraImposter &operator=(const FloraImposter &) = delete;

    // Atlas for the layer's mesh+materials: the first request schedules the bake (ready=false,
    // draw the mesh for now). Requires a loaded mesh.
    View ensure(AssetRef<ImportedMeshAsset> mesh, AssetRef<MaterialAsset> overrideMaterial);

    // One view angle per frame (called by the flora renderer before the scene submit).
    void processBaking();

private:
    struct Entry {
        AssetId meshId = 0;
        AssetId overrideId = 0;
        std::vector<AssetId> materialIds; // per-primitive (cache key)
        // Keeps the assets alive for the lifetime of the atlas (a rebake on change is a new entry).
        AssetRef<ImportedMeshAsset> mesh;
        AssetRef<MaterialAsset> overrideMaterial;
        AssetRef<TextureAsset> colorAtlas;  // bake render targets = billboard textures
        AssetRef<TextureAsset> normalAtlas;
        Miren::TextureHandle colorDepth{MIREN_INVALID_HANDLE};
        Miren::TextureHandle normalDepth{MIREN_INVALID_HANDLE};
        Miren::FrameBufferHandle colorFb{MIREN_INVALID_HANDLE};
        Miren::FrameBufferHandle normalFb{MIREN_INVALID_HANDLE};
        int bakedTiles = 0; // == TILES → done (FB/depth released)
        glm::vec3 boundsMin{0.f};
        glm::vec3 boundsMax{1.f};
        float crownMetallic = 0.f;   // crown surface of the species (see View)
        float crownRoughness = 1.f;
        float crownTranslucency = 0.f;
    };

    void bakeTile(Entry &entry, int tile);
    void releaseBakeResources(Entry &entry);

    std::vector<std::unique_ptr<Entry>> m_entries;
};

} // namespace Panda
