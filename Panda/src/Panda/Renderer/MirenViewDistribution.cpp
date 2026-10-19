#include "Panda/Renderer/MirenViewDistribution.hpp"

namespace Panda {

namespace Views {

    Miren::ViewId GLOBAL_VIEW = 0;
    Miren::ViewId GAME_UI_VIEW = 1; // after the tonemap and the editor composite: UI in display space
    Miren::ViewId IDS_VIEW = 2;
    Miren::ViewId EDITOR_VIEWPORT_VIEW = 3;
    // Auto exposure: measurement 64×64 → reduction 1×1 → adaptation → application (before bloom).
    Miren::ViewId EXPOSURE_MEASURE_VIEW = 29;
    Miren::ViewId EXPOSURE_REDUCE_VIEW = 28;
    Miren::ViewId EXPOSURE_ADAPT_VIEW = 27;
    Miren::ViewId EXPOSURE_APPLY_VIEW = 26;
    Miren::ViewId BLOOM_VIEWS_BEGIN = 25; // bloom pyramid 25..18 (downsample+upsample+apply, before the chain)
    Miren::ViewId POST_PROCESSING_VIEWS_BEGIN = 17; // passes 17..9 (8 user ones + Tonemap)
    Miren::ViewId POST_PROCESSING_OUTPUT_VIEW = 8;
    Miren::ViewId SCENE_VIEW = 30;
    // The emissive pass of the Lab preview: the sun disc into a separate texture with the SHARED
    // depth of the preview scene — after PREVIEW_RENDER_VIEW (39), before the bloom pyramid (29..22).
    Miren::ViewId PREVIEW_EMISSIVE_VIEW = 31;
    Miren::ViewId SELECTED_GEOMETRY_VIEW = 32;
    Miren::ViewId SHADOW_VIEWS_BEGIN = 36; // CSM cascades 36..33 — before the scene (it reads the atlas)
    Miren::ViewId PREVIEW_TONEMAP_VIEW = 37; // finalization of previews/thumbnails: HDR scratch → display texture
    Miren::ViewId THUMBNAIL_TONEMAP_VIEW = 38;
    Miren::ViewId PREVIEW_RENDER_VIEW = 39;
    Miren::ViewId THUMBNAIL_RENDER_VIEW = 40;
    Miren::ViewId FLORA_IMPOSTER_COLOR_VIEW = 41;
    Miren::ViewId FLORA_IMPOSTER_NORMAL_VIEW = 42;
    Miren::ViewId SHOOT_BAKE_VIEWS_BEGIN = 60;
    Miren::ViewId SHOOT_BAKE_VIEWS_END = 43;
    Miren::ViewId BRANCH_BAKE_VIEWS_BEGIN = 80; // 80..61: geometry, seed, ≤16 JFA, finish
    Miren::ViewId BRANCH_BAKE_VIEWS_END = 61;

} // namespace Views

} // namespace Panda
