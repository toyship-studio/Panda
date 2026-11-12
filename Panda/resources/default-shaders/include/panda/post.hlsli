// PANDA_POST — the data of a postprocessing pass (PostprocessingPipeline): resolutionTime (xy — the
// target size of THIS pass in pixels, z — time in seconds), cameraNearFar (x — near, y — far),
// sunScreen (xy — the sun on screen in UV, z — 1 when it is in the frame). The block is generated
// from Panda::PostUbo.
#include "panda/blocks/post.hlsli"
