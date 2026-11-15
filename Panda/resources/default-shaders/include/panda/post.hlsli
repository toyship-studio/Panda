// PANDA_POST — the data of a postprocessing pass (PostprocessingPipeline): resolutionTime (xy — the
// target size of THIS pass in pixels, z — time in seconds), cameraNearFar (x — near, y — far),
// sunScreen (xy — the sun on screen in UV, z — 1 when it is in the frame), tonemapParams (x — the
// tonemap curve of the camera: 0 AgX, 1 ACES, 2 neutral, 3 Reinhard, 4 none; y — the exposure
// multiplier of the camera, applied by the pass that tonemaps). The block is generated from
// Panda::PostUbo.
#include "panda/blocks/post.hlsli"
