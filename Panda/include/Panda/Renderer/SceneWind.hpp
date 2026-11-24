#pragma once

#include "Panda/Base/Base.hpp"

namespace Panda {

// The wind of a frame: the main WindComponent of the world drawn (WorldView::render), calm in a
// world without one; an application without a world keeps its own.
struct SceneWindSettings final {
    float strength = 0.25f;       // 0..1 — overall strength
    float speed = 1.6f;           // tempo of gusts and flutter
    float gustiness = 0.6f;       // contrast: 0 — steady flow, 1 — lulls and squalls
    float directionDegrees = 0.f; // where it blows to, in XZ (0 = +X)
};

// The shared scene wind — one per application. The strength in the vegetation shaders is the
// sum of three layers: a global envelope (the whole scene picks up and dies down together) + a
// local wave (a gust runs across the world along the flow) + per-element flutter (the phase
// comes from the hash of a grass blade/leaf/card — neighbors sway out of sync). The phases
// accumulate CONTINUOUSLY (editing Speed does not teleport the picture) and wrap with a period
// of 3003 — to match the periodized value noise of the shaders (see grass.vert); the
// accumulation is lazy, by application time, on reading phaseVector (there are no hooks in
// Application). apply() publishes the strength and the gustiness into the system material
// fields windStrength / windGustiness — the engine sets the fields of that name in all
// materials. The wind of a world is applied when a frame of it begins (WorldView::render): what
// a world draws is drawn in its wind. An application without a world applies its own settings before it
// renders. Without the wind shader feature (ShaderFeatures) the wind is calm for everyone who
// asks: the applied strength is zero and the phases stand still.
class SceneWind final {
public:
    static void apply(const SceneWindSettings &settings);
    // The wind as applied: the strength is zero without the wind feature.
    static const SceneWindSettings &settings();
    // Phase pack of the wind block (WindUbo): xy — unit direction (XZ), z — travel of the gust
    // wave (m), w — flutter phase. The first call of a frame advances the phases by the frame dt.
    static Vec4 phaseVector();
};

} // namespace Panda
