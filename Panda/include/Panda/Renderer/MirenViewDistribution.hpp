#pragma once

#include <Miren/Miren.hpp>

namespace Panda {

// IMPORTANT: Miren sorts draw calls by DESCENDING viewId (Frame::sort) — a larger id is executed
// earlier. The layout below reflects the frame order: thumbnails/previews → scene → postprocessing
// → editor output (the outline reads the result of the scene/chain) → IDS → game UI → UI
// (GLOBAL_VIEW last).
namespace Views {

    extern Miren::ViewId GLOBAL_VIEW;
    // Game PandaUI — AFTER the tonemap, in display space: in the runtime on top of the backbuffer,
    // in the editor on top of the viewport's output texture (the EDITOR_VIEWPORT_VIEW composite).
    extern Miren::ViewId GAME_UI_VIEW;
    extern Miren::ViewId SCENE_VIEW;
    extern Miren::ViewId EDITOR_VIEWPORT_VIEW;
    extern Miren::ViewId SELECTED_GEOMETRY_VIEW;
    extern Miren::ViewId IDS_VIEW;
    // Depth passes of the shadow cascades (World::renderShadowCascades): shadowView(0..MAX-1),
    // ids descend from BEGIN; executed before the scene, which samples the atlas.
    extern Miren::ViewId SHADOW_VIEWS_BEGIN;
    extern Miren::ViewId PREVIEW_RENDER_VIEW;
    extern Miren::ViewId PREVIEW_EMISSIVE_VIEW;
    extern Miren::ViewId THUMBNAIL_RENDER_VIEW;
    // Baking of the flora impostor atlases (FloraImposter): color and normals — before the scene,
    // one view angle per frame.
    extern Miren::ViewId FLORA_IMPOSTER_COLOR_VIEW;
    extern Miren::ViewId FLORA_IMPOSTER_NORMAL_VIEW;
    // Reserved descending range for Shoot rasterization, resolve and edge padding.
    extern Miren::ViewId SHOOT_BAKE_VIEWS_BEGIN;
    extern Miren::ViewId SHOOT_BAKE_VIEWS_END;
    // Reserved range for a branch-to-card baker of an application built on the engine (atlas
    // geometry, seeds, jump flooding and the finish) — a range of its own, so as not to conflict
    // with the shoot baker in the same frame; executed before the preview (it draws the atlas
    // right away).
    extern Miren::ViewId BRANCH_BAKE_VIEWS_BEGIN;
    extern Miren::ViewId BRANCH_BAKE_VIEWS_END;
    // Finalization of previews/thumbnails (tonemap of the HDR scratch target into the display
    // texture) — after the render views above.
    extern Miren::ViewId PREVIEW_TONEMAP_VIEW;
    extern Miren::ViewId THUMBNAIL_TONEMAP_VIEW;
    // Pyramid of the built-in bloom (BloomRenderer): 2*MAX_LEVELS steps before the user chain.
    // Auto exposure (ExposureRenderer): 4 steps before bloom.
    extern Miren::ViewId EXPOSURE_MEASURE_VIEW;
    extern Miren::ViewId EXPOSURE_REDUCE_VIEW;
    extern Miren::ViewId EXPOSURE_ADAPT_VIEW;
    extern Miren::ViewId EXPOSURE_APPLY_VIEW;
    extern Miren::ViewId BLOOM_VIEWS_BEGIN;
    // Postprocessing passes: postProcessingView(0..MAX), each with its own FBO; ids descend from
    // BEGIN. MAX_PASSES+1 slots: the last one is the implicit final Tonemap.
    extern Miren::ViewId POST_PROCESSING_VIEWS_BEGIN;
    // Runtime chain output (the final pass — straight to the screen); executed after all passes.
    extern Miren::ViewId POST_PROCESSING_OUTPUT_VIEW;

    inline Miren::ViewId shadowView(int cascadeIndex) {
        return SHADOW_VIEWS_BEGIN - cascadeIndex;
    }

    inline Miren::ViewId bloomView(int stepIndex) {
        return BLOOM_VIEWS_BEGIN - stepIndex;
    }

    inline Miren::ViewId postProcessingView(int passIndex) {
        return POST_PROCESSING_VIEWS_BEGIN - passIndex;
    }

} // namespace Views

} // namespace Panda
