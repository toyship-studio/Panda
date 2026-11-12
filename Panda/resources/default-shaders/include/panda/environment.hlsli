// PANDA_ENVIRONMENT — the environment map of the world (shader feature `environment`): the system
// texture environmentMap (equirect: the top of the image is the zenith, u = atan2(z, x); without a
// map of the world the slot holds the built-in sky gradient) and environmentParams (x — exposure,
// y — the highest mip of the map, z — 1 when the world has a map, else 0: no reflections). The
// block is generated from Panda::EnvironmentUbo; with the feature off the include is a compile
// error.
#include "panda/blocks/environment.hlsli"
