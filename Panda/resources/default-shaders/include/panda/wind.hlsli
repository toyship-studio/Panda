// PANDA_WIND — the scene wind (shader feature `wind`, SceneWind): sceneWind.xy — the unit direction
// in XZ (world), z — the travel of the gust wave (m), w — the flutter phase. The phases accumulate
// on the CPU with a wrap of 3003 — a shader periodizes its noise to that (see veg_grass.vert). The
// strength and the gustiness are the system material fields windStrength / windGustiness. The
// block is generated from Panda::WindUbo; with the feature off the include is a compile error.
#include "panda/blocks/wind.hlsli"
