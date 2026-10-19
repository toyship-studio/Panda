#pragma once

#include "Config.hpp"
#include "VertexBufferLayoutData.hpp"

#include <Foundation/Foundation.hpp>
#include <cstdint>
#include <cstdlib>

#if MIREN_LOG_ENABLED == 1
#    define MIREN_LOG(...) LOG_INFO(__VA_ARGS__)
#else
#    define MIREN_LOG(...)
#endif

#if MIREN_CMDBUF_LOG_ENABLED == 1
#    define CMDBUF_LOG(...) LOG_INFO(__VA_ARGS__)
#else
#    define CMDBUF_LOG(...)
#endif

#define MIREN_INVALID_HANDLE UINT16_MAX

namespace Miren {

// Snapshot of the renderer's GPU metrics for status indicators (zeros — not supported by the backend).
struct GpuStats {
    uint64_t allocatedBytes = 0; // device memory allocated by the process (VRAM)
    double frameMs = 0.0;        // GPU time of the last completed frame
};

#define MIREN_HANDLE(name)                                                                                             \
    struct name {                                                                                                      \
        name(uint16_t id)                                                                                              \
            : id(id) {}                                                                                                \
        name()                                                                                                         \
            : id(MIREN_INVALID_HANDLE) {}                                                                              \
        uint16_t id;                                                                                                   \
        bool isValid() const {                                                                                         \
            return id != MIREN_INVALID_HANDLE;                                                                         \
        }                                                                                                              \
    };

using ViewId = uint16_t;
MIREN_HANDLE(ProgramHandle)
MIREN_HANDLE(TextureHandle)
MIREN_HANDLE(IndexBufferHandle)
MIREN_HANDLE(FrameBufferHandle)
MIREN_HANDLE(VertexBufferHandle)
MIREN_HANDLE(VertexLayoutHandle)

enum TextureFormat {
    // Color
    R8,
    R8I,
    R8UI,
    RGB8,
    RGBA8,
    R32I,
    R32UI,
    RGBA16F,
    // Depth/stencil
    DEPTH24STENCIL8,
    // sRGB-encoded color: a sample is decoded to linear in hardware (before filtering).
    // At the end of the list — the numeric format values are serialized in cooked textures.
    RGBA8_SRGB,
    // Depth without stencil (shadow map): sampled as depth (comparison sampler).
    DEPTH32F,
    // Two 8-bit channels (crown occlusion volume: sky visibility + sun transmittance);
    // when sampled, b = 0, a = 1.
    RG8
};

// CLAMP_TO_BORDER — white (1,1,1,1) beyond the texture edge: the shadow map outside the atlas
// reads as "lit". Border colors are a device capability (Metal: Apple GPU family 7 and later, the
// Mac family); a device without them clamps to the edge instead.
enum TextureWrapMode { CLAMP, REPEAT, CLAMP_TO_BORDER };

// Comparison sampler (shadow map): a sample returns the result of comparing ref with
// the texel (hardware PCF). In HLSL — SamplerComparisonState + SampleCmp.
enum TextureCompareMode { COMPARE_NONE, COMPARE_LESS_EQUAL };

enum TextureFiltering {
    NEAREST,
    LINEAR,
    NEAREST_MIPMAP_NEAREST,
    NEAREST_MIPMAP_LINEAR,
    LINEAR_MIPMAP_NEAREST,
    LINEAR_MIPMAP_LINEAR
};

struct ProgramCreate {
    Foundation::Memory m_vertexBinary;
    Foundation::Memory m_fragmentBinary;
    uint32_t m_vertexBinarySize = 0;
    uint32_t m_fragmentBinarySize = 0;
};

struct TextureCreate {
    TextureFormat m_format;
    TextureFiltering m_minFiltering;
    TextureFiltering m_magFiltering;
    TextureWrapMode m_wrap;
    TextureCompareMode m_compare;
    uint16_t m_width;
    uint16_t m_height;
    // 3D texture (volume, modeled after bgfx createTexture3D): depth in texels, 0 — a regular
    // 2D/cube texture. The creation data and the updateTexture data are width×height slices in a
    // row, m_depth of them; a box — updateTextureRegion3D. In HLSL — Texture3D + SamplerState:
    // hardware trilinear filtering, wrap along all three axes. A volume is never a render target,
    // has no mips, is not resized and is not read by readTexture.
    uint16_t m_depth;
    uint8_t m_numMips;
    bool m_isCubeMap;
    Foundation::Memory m_data;

    TextureCreate()
        : m_format(TextureFormat::RGBA8)
        , m_minFiltering(TextureFiltering::NEAREST)
        , m_magFiltering(TextureFiltering::NEAREST)
        , m_wrap(TextureWrapMode::REPEAT)
        , m_compare(TextureCompareMode::COMPARE_NONE)
        , m_width(1)
        , m_height(1)
        , m_depth(0)
        , m_numMips(0)
        , m_isCubeMap(false)
        , m_data(nullptr) {}

    bool isDepthFormat() const {
        return m_format == DEPTH24STENCIL8 || m_format == DEPTH32F;
    }

    bool is3D() const {
        return m_depth > 0;
    }

    size_t bytesPerTexel() const {
        switch (m_format) {
            case R8:
            case R8I:
            case R8UI:
                return 1;
            case RGB8:
                return 3;
            case RGBA8:
            case RGBA8_SRGB:
                return 4;
            case R32I:
            case R32UI:
                return 4;
            case RGBA16F:
                return 8;
            case DEPTH24STENCIL8:
            case DEPTH32F:
                return 4;
            case RG8:
                return 2;
        }
        return 4;
    }
};

enum class RenderPassInputType { UniformBuffer, ImageSampler2D, ImageSamplerCube, Count };

struct FrameBufferAttachment {
    FrameBufferAttachment()
        : handle(MIREN_INVALID_HANDLE) {}

    FrameBufferAttachment(TextureHandle handle)
        : handle(handle) {}

    TextureHandle handle;
};

struct FrameBufferSpecification {
    FrameBufferSpecification()
        : attachments()
        , num(0) {}

    FrameBufferSpecification(FrameBufferAttachment *_attachments, uint32_t num)
        : attachments()
        , num(num) {
        PND_ASSERT(num <= MAX_FRAMEBUFFER_ATTACHMENTS, "MAX ATTACHMENTS NUMBER EXCEEDED");
        for (int i = 0; i < num; i++) {
            attachments[i] = _attachments[i];
        }
    }

    FrameBufferAttachment attachments[MAX_FRAMEBUFFER_ATTACHMENTS];
    uint32_t num;
};

struct TransientIndexBuffer {
    TransientIndexBuffer()
        : data(nullptr)
        , size(0)
        , startIndex(0)
        , handle(MIREN_INVALID_HANDLE)
        , elementType(BufferElementType::UnsignedInt) {}
    uint8_t *data;
    uint32_t size;
    uint32_t startIndex;
    IndexBufferHandle handle;
    BufferElementType elementType;
};

struct TransientVertexBuffer {
    TransientVertexBuffer()
        : data(nullptr)
        , size(0)
        , startVertex(0)
        , handle(MIREN_INVALID_HANDLE) {}
    uint8_t *data;
    uint32_t size;
    uint32_t startVertex;
    VertexBufferHandle handle;
};

struct Size {
    int width;
    int height;

    Size()
        : width(0)
        , height(0) {}

    Size(int width, int height)
        : width(width)
        , height(height) {}

    inline bool isZero() {
        return width == 0 && height == 0;
    }
};

struct Point {
    int x;
    int y;

    Point()
        : x(0)
        , y(0) {}

    Point(int x, int y)
        : x(x)
        , y(y) {}

    inline bool isZero() const {
        return x == 0 && y == 0;
    }
};

struct Rect {
    Point origin;
    Size size;

    Rect()
        : origin()
        , size() {}

    Rect(int x, int y, int width, int height)
        : origin(x, y)
        , size(width, height) {}

    inline static Rect zero() {
        return {};
    }

    inline bool isZero() {
        return origin.isZero() && size.isZero();
    }
};

struct Clear {
    int attachmentIndex;
    uint32_t value;

    Clear(int attachmentIndex, uint32_t value)
        : attachmentIndex(attachmentIndex)
        , value(value) {}
};

} // namespace Miren
