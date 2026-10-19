#pragma once

#include <Foundation/PlatformDetection.hpp>

#include <cstdint>

#if defined(PLATFORM_MACOS) || defined(PLATFORM_IOS)

#    include "Miren/Base.hpp"
#    include "Miren/VertexBufferLayoutData.hpp"

#    include <map>
#    include <string>
#    include <unordered_map>
#    include <vector>

namespace Miren {

// One shader program: VS + FS compiled from spirv-cross MSL source at creation time
// (entry points are renamed to main0 by spirv-cross). Unlike D3D11, blending and
// attachment formats are baked into MTLRenderPipelineState, so pipelines are cached
// per (vertex layout, blend, attachment formats). Argument reflection (name → index,
// per stage) is captured from the first pipeline built — indices are identical
// across variants of the same program.
class MetalShader final {
public:
    struct StageSlots {
        int vertex = -1;
        int fragment = -1;
    };

    // Key of a PSO variant: attachment formats of the current pass + blend + vertex layout
    // (+ the instance stream layout; MIREN_INVALID_HANDLE = no instancing).
    struct PipelineKey {
        uint16_t layoutId = 0;
        uint16_t instanceLayoutId = MIREN_INVALID_HANDLE;
        bool blend = false;
        bool colorWrite = true; // false — depth-only pass (writeMask None)
        uint32_t colorFormats[MAX_FRAMEBUFFER_ATTACHMENTS] = {}; // MTLPixelFormat
        uint32_t colorCount = 0;
        uint32_t depthFormat = 0; // MTLPixelFormatInvalid if there is no depth

        bool operator<(const PipelineKey &other) const;
    };

    MetalShader();

    void create(void *device, ProgramCreate create);
    void terminate();
    bool isValid() const;

    // id<MTLRenderPipelineState> or nullptr on a build error. instanceLayout != nullptr —
    // the instance stream attributes are added after the vertex ones (the locations continue the
    // numbering).
    void *getPipeline(
        void *device,
        const PipelineKey &key,
        const VertexBufferLayoutData &layout,
        const VertexBufferLayoutData *instanceLayout = nullptr
    );

    const StageSlots *findBuffer(const char *name);
    const StageSlots *findTexture(const char *name);
    // The sampler of the "<texture>Sampler" pair; nullptr if the shader has no samplers.
    const StageSlots *findSamplerForTexture(const char *textureName);

private:
    const StageSlots *findSlots(
        const std::unordered_map<std::string, StageSlots> &map, const char *name, const char *kind, bool logMissing
    );
    void captureReflection(void *reflection);

    void *m_vertexFunction;   // id<MTLFunction>
    void *m_fragmentFunction; // id<MTLFunction>
    std::map<PipelineKey, void *> m_pipelines; // id<MTLRenderPipelineState>
    bool m_reflected;
    std::unordered_map<std::string, StageSlots> m_buffers;
    std::unordered_map<std::string, StageSlots> m_textures;
    std::unordered_map<std::string, StageSlots> m_samplers;
    std::vector<std::string> m_missingInputLog; // names already reported, log once
};

} // namespace Miren

#endif
