#pragma once

#include <Miren/Miren.hpp>

#include "Panda/Renderer/FullscreenQuad.hpp"

namespace Panda {

// Settings of the camera's auto exposure (CameraComponent.autoExposure*).
struct ExposureSettings {
    bool enabled = false;
    float key = 0.18f;         // target average luminance of the frame (gray card)
    float speed = 2.f;         // eye adaptation speed, 1/s
    float minExposure = 0.25f; // limits of amplification/attenuation
    float maxExposure = 4.f;
};

// Auto exposure / eye adaptation: measuring the average luminance of the frame (64×64 → 1×1),
// exponential adaptation over time (1×1 ping-pong, no CPU readbacks) and multiplying the
// frame by exposure = key / adaptedLum. Executed by PostprocessingPipeline as the FIRST step
// (before bloom): the rest of the pipeline sees an already exposed frame.
class ExposureRenderer final {
public:
    ExposureRenderer();
    ~ExposureRenderer();

    // Returns the full-res exposed frame (16F, linear light). Disabled/unavailable — the input
    // as is. time — the frame's seconds (the adaptation dt is computed from the difference).
    Miren::TextureHandle apply(
        Miren::TextureHandle sceneColor, uint32_t width, uint32_t height,
        const ExposureSettings &settings, float time, FullscreenQuad &quad
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

    Target m_measure;    // 64×64 luminance measurement
    Target m_reduce;     // 1×1 frame average
    Target m_adapted[2]; // 1×1 adaptation ping-pong
    Target m_output;     // full-res exposed frame
    int m_adaptedIndex = 0;
    bool m_adaptedValid = false; // first frame — snap without creep
    float m_lastTime = 0.f;
};

} // namespace Panda
