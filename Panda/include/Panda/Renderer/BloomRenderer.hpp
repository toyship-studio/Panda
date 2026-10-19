#pragma once

#include <glm/glm.hpp>

#include <Miren/Miren.hpp>

#include "Panda/Renderer/FullscreenQuad.hpp"

namespace Panda {

// Settings of the camera's built-in bloom (CameraComponent.bloom*).
struct BloomSettings {
    bool enabled = false;
    float intensity = 1.f;
    float radius = 1.f; // tent filter spread on the upsample, in lower-level texels (~0.5..2)
};

// Built-in bloom: a classic pyramid in linear HDR. Downsampling from the emissive buffer in ×2
// steps with a box filter (every source pixel contributes — no decimation and no flicker),
// upsampling with octave accumulation through a tent filter (bright core + wide tail), apply adds
// the glow to the scene color with intensity. There is no brightness threshold: "what glows" is
// defined by the material's emissive.
// Executed by PostprocessingPipeline BEFORE the user chain (Views::bloomView).
class BloomRenderer final {
public:
    static constexpr int MAX_LEVELS = 4; // ½..1/16; 2*MAX_LEVELS steps — one per bloom view

    BloomRenderer();
    ~BloomRenderer();

    // Runs the pyramid; returns the full-res "scene color + bloom" texture (16F, linear light).
    // With bloom disabled / a zero-sized frame / unavailable shaders returns sceneColor as is.
    Miren::TextureHandle apply(
        Miren::TextureHandle sceneColor,
        Miren::TextureHandle sceneEmissive,
        uint32_t width,
        uint32_t height,
        const BloomSettings &settings,
        FullscreenQuad &quad // shared quad of the pipeline (ingredient: infrastructure is outside)
    );

private:
    struct Target {
        Miren::FrameBufferHandle frameBuffer;
        Miren::FrameBufferSpecification specification;
        Miren::TextureHandle color;
        uint32_t width = 0;
        uint32_t height = 0;
        bool created = false;
    };
    void ensureTarget(Target &target, uint32_t width, uint32_t height);

    Target m_down[MAX_LEVELS];     // downsample levels: ½, ¼, ⅛, 1/16
    Target m_up[MAX_LEVELS - 1];   // upsample levels with accumulation
    Target m_output;               // full-res: scene color + bloom
};

} // namespace Panda
