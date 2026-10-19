#include "MetalResources.hpp"

#if defined(PLATFORM_MACOS) || defined(PLATFORM_IOS)

#    import <Metal/Metal.h>

#    include <algorithm>
#    include <cmath>
#    include <cstring>
#    include <map>
#    include <vector>

namespace Miren {

namespace {

    template<typename T>
    void safeRelease(T &object) {
        if (object) {
            [(id)object release];
            object = nullptr;
        }
    }

    id<MTLBuffer> createBuffer(id<MTLDevice> device, const void *data, uint32_t size) {
        if (size == 0) { size = 4; } // Metal forbids a zero size, transients are created empty
        id<MTLBuffer> buffer =
            data ? [device newBufferWithBytes:data length:size options:MTLResourceStorageModeShared]
                 : [device newBufferWithLength:size options:MTLResourceStorageModeShared];
        if (buffer == nil) { LOG_ERROR("METAL: Failed to create buffer (%u bytes)", size); }
        return buffer;
    }

    MTLPixelFormat pixelFormat(TextureFormat format) {
        switch (format) {
            case R8:
                return MTLPixelFormatR8Unorm;
            case R8I:
                return MTLPixelFormatR8Sint;
            case R8UI:
                return MTLPixelFormatR8Uint;
            case RGB8: // expanded to RGBA8 at upload, Metal has no 24-bit format
            case RGBA8:
                return MTLPixelFormatRGBA8Unorm;
            case RGBA8_SRGB: // hardware sRGB→linear decode on sample
                return MTLPixelFormatRGBA8Unorm_sRGB;
            case R32I:
                return MTLPixelFormatR32Sint;
            case R32UI:
                return MTLPixelFormatR32Uint;
            case RGBA16F:
                return MTLPixelFormatRGBA16Float;
            case DEPTH24STENCIL8:
                // There is no D24S8 on Apple silicon; 32F+S8 works everywhere.
                return MTLPixelFormatDepth32Float_Stencil8;
            case DEPTH32F:
                return MTLPixelFormatDepth32Float;
            case RG8:
                return MTLPixelFormatRG8Unorm;
        }
        return MTLPixelFormatInvalid;
    }

    MTLSamplerMinMagFilter minFilter(TextureFiltering filter) {
        const bool linear =
            filter != NEAREST && filter != NEAREST_MIPMAP_NEAREST && filter != NEAREST_MIPMAP_LINEAR;
        return linear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
    }

    MTLSamplerMipFilter mipFilter(TextureFiltering filter) {
        // GL parity: a min filter without the _MIPMAP_ suffix does not sample mips at all
        // (GL_NEAREST/GL_LINEAR), even if the texture has them. Otherwise mip seams show up on
        // Metal that are not visible on OpenGL (the Neverland terrain: numMips=4 with a NEAREST
        // filter).
        switch (filter) {
            case NEAREST_MIPMAP_LINEAR:
            case LINEAR_MIPMAP_LINEAR:
                return MTLSamplerMipFilterLinear;
            case NEAREST_MIPMAP_NEAREST:
            case LINEAR_MIPMAP_NEAREST:
                return MTLSamplerMipFilterNearest;
            case NEAREST:
            case LINEAR:
                break;
        }
        return MTLSamplerMipFilterNotMipmapped;
    }

    // Sampler border colors are a device capability: Apple GPU family 7 (A14, M1) and later, and the
    // Mac family (Intel and AMD). The paravirtual GPU of a macOS virtual machine (the CI runners) has
    // none, and the Metal validation layer stops the process on a descriptor that asks for one.
    bool supportsBorderColor(id<MTLDevice> device) {
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Wdeprecated-declarations" // Mac2: the Intel and AMD Macs the engine still runs on
        static const bool supported =
            [device supportsFamily:MTLGPUFamilyApple7] || [device supportsFamily:MTLGPUFamilyMac2];
#    pragma clang diagnostic pop
        static bool reported = false;
        if (!supported && !reported) {
            reported = true;
            LOG_INFO("METAL: the device has no sampler border colors; clamp-to-border samplers clamp to the edge");
        }
        return supported;
    }

    // Without border colors CLAMP_TO_BORDER degrades to clamp-to-edge: a shadow map beyond its atlas
    // reads the edge depth instead of "lit" (Miren/Base.hpp, TextureWrapMode).
    MTLSamplerAddressMode addressMode(TextureWrapMode wrap, bool borderColors) {
        switch (wrap) {
            case CLAMP: return MTLSamplerAddressModeClampToEdge;
            case REPEAT: return MTLSamplerAddressModeRepeat;
            case CLAMP_TO_BORDER:
                return borderColors ? MTLSamplerAddressModeClampToBorderColor : MTLSamplerAddressModeClampToEdge;
        }
        return MTLSamplerAddressModeRepeat;
    }

    // Sampler states are immutable; a handful of filter/wrap combinations exist,
    // so they are cached for the process lifetime.
    id<MTLSamplerState> acquireSampler(id<MTLDevice> device, const TextureCreate &create) {
        const MTLSamplerMinMagFilter min = minFilter(create.m_minFiltering);
        const MTLSamplerMinMagFilter mag =
            create.m_magFiltering == NEAREST ? MTLSamplerMinMagFilterNearest : MTLSamplerMinMagFilterLinear;
        const MTLSamplerMipFilter mip = mipFilter(create.m_minFiltering);
        const MTLSamplerAddressMode address = addressMode(create.m_wrap, supportsBorderColor(device));
        const bool compare = create.m_compare == COMPARE_LESS_EQUAL;
        static std::map<uint32_t, id<MTLSamplerState>> s_samplers;
        const uint32_t key = static_cast<uint32_t>(min) | (static_cast<uint32_t>(mag) << 4) |
                             (static_cast<uint32_t>(mip) << 8) | (static_cast<uint32_t>(address) << 12) |
                             (compare ? 1u << 16 : 0u);
        auto it = s_samplers.find(key);
        if (it != s_samplers.end()) { return it->second; }
        MTLSamplerDescriptor *descriptor = [[MTLSamplerDescriptor alloc] init];
        descriptor.minFilter = min;
        descriptor.magFilter = mag;
        descriptor.mipFilter = mip;
        descriptor.sAddressMode = address;
        descriptor.tAddressMode = address;
        descriptor.rAddressMode = address;
        // The border is white: the shadow map beyond the edge reads as "lit". Set only with the border
        // mode: a device without border colors rejects the field under validation.
        if (address == MTLSamplerAddressModeClampToBorderColor) {
            descriptor.borderColor = MTLSamplerBorderColorOpaqueWhite;
        }
        // Comparison sampler (shadow map): SampleCmp in HLSL → depth2d + compare in MSL.
        descriptor.compareFunction = compare ? MTLCompareFunctionLessEqual : MTLCompareFunctionNever;
        id<MTLSamplerState> sampler = [device newSamplerStateWithDescriptor:descriptor];
        [descriptor release];
        if (sampler == nil) {
            LOG_ERROR("METAL: Failed to create sampler state");
            return nil;
        }
        s_samplers[key] = sampler;
        return sampler;
    }

    // Metal has no 24-bit RGB format; expand to RGBA with opaque alpha.
    std::vector<uint8_t> expandRgbToRgba(const uint8_t *rgb, size_t texelCount) {
        std::vector<uint8_t> rgba(texelCount * 4);
        for (size_t i = 0; i < texelCount; i++) {
            rgba[i * 4 + 0] = rgb[i * 3 + 0];
            rgba[i * 4 + 1] = rgb[i * 3 + 1];
            rgba[i * 4 + 2] = rgb[i * 3 + 2];
            rgba[i * 4 + 3] = 255;
        }
        return rgba;
    }

    // Upload of a 3D texture box: region.size.depth slices in a row (bytesPerImage is the slice
    // pitch), RGB8 is expanded to RGBA8.
    void replaceVolumeRegion(
        id<MTLTexture> texture, const TextureCreate &create, MTLRegion region, const uint8_t *pixels
    ) {
        const size_t texelCount = region.size.width * region.size.height * region.size.depth;
        const bool expand = create.m_format == RGB8;
        std::vector<uint8_t> expanded;
        if (expand) {
            expanded = expandRgbToRgba(pixels, texelCount);
            pixels = expanded.data();
        }
        const uint32_t rowPitch =
            static_cast<uint32_t>(region.size.width) * (expand ? 4u : static_cast<uint32_t>(create.bytesPerTexel()));
        [texture replaceRegion:region
                   mipmapLevel:0
                         slice:0
                     withBytes:pixels
                   bytesPerRow:rowPitch
                 bytesPerImage:rowPitch * region.size.height];
    }

} // namespace

// ----- MetalVertexBuffer -----------------------------------------------------

MetalVertexBuffer::MetalVertexBuffer()
    : m_buffers()
    , m_capacities()
    , m_slot(0)
    , m_layoutHandle(MIREN_INVALID_HANDLE)
    , m_isDynamic(false) {}

void MetalVertexBuffer::create(
    void *device, Foundation::Memory data, uint32_t size, VertexLayoutHandle layout, bool isDynamic
) {
    m_layoutHandle = layout;
    m_isDynamic = isDynamic;
    m_slot = 0;
    m_capacities[0] = size;
    m_buffers[0] = (void *)createBuffer((id<MTLDevice>)device, data.data, size);
    data.release();
}

void MetalVertexBuffer::update(void *device, const void *data, uint32_t size) {
    if (!m_isDynamic) {
        LOG_ERROR("METAL: Attempt to update a static vertex buffer");
        return;
    }
    // Ring: write into the next slot — the previous ones may be read by frames in flight.
    m_slot = (m_slot + 1) % METAL_DYNAMIC_BUFFER_RING;
    if (size > m_capacities[m_slot] || !m_buffers[m_slot]) {
        safeRelease(m_buffers[m_slot]);
        m_capacities[m_slot] = std::max(size, m_capacities[m_slot] * 2);
        m_buffers[m_slot] = (void *)createBuffer((id<MTLDevice>)device, nullptr, m_capacities[m_slot]);
    }
    if (m_buffers[m_slot] && size > 0) {
        std::memcpy([(id<MTLBuffer>)m_buffers[m_slot] contents], data, size);
    }
}

void MetalVertexBuffer::terminate() {
    for (uint32_t i = 0; i < METAL_DYNAMIC_BUFFER_RING; i++) {
        safeRelease(m_buffers[i]);
        m_capacities[i] = 0;
    }
    m_slot = 0;
    m_layoutHandle = VertexLayoutHandle();
}

// ----- MetalIndexBuffer ------------------------------------------------------

MetalIndexBuffer::MetalIndexBuffer()
    : m_buffers()
    , m_capacities()
    , m_slot(0)
    , m_elementType(BufferElementType::UnsignedInt)
    , m_isDynamic(false) {}

uint32_t MetalIndexBuffer::getIndexSize() const {
    return m_elementType == BufferElementType::UnsignedShort ? 2 : 4;
}

uint32_t MetalIndexBuffer::getIndexType() const {
    return m_elementType == BufferElementType::UnsignedShort ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32;
}

void MetalIndexBuffer::create(
    void *device, Foundation::Memory data, BufferElementType elementType, size_t count, bool isDynamic
) {
    m_elementType = elementType;
    m_isDynamic = isDynamic;
    m_slot = 0;
    const uint32_t size = static_cast<uint32_t>(count * getIndexSize());
    m_capacities[0] = size;
    m_buffers[0] = (void *)createBuffer((id<MTLDevice>)device, data.data, size);
    data.release();
}

void MetalIndexBuffer::update(void *device, const void *data, size_t count) {
    if (!m_isDynamic) {
        LOG_ERROR("METAL: Attempt to update a static index buffer");
        return;
    }
    const uint32_t size = static_cast<uint32_t>(count * getIndexSize());
    // Ring: write into the next slot — the previous ones may be read by frames in flight.
    m_slot = (m_slot + 1) % METAL_DYNAMIC_BUFFER_RING;
    if (size > m_capacities[m_slot] || !m_buffers[m_slot]) {
        safeRelease(m_buffers[m_slot]);
        m_capacities[m_slot] = std::max(size, m_capacities[m_slot] * 2);
        m_buffers[m_slot] = (void *)createBuffer((id<MTLDevice>)device, nullptr, m_capacities[m_slot]);
    }
    if (m_buffers[m_slot] && size > 0) {
        std::memcpy([(id<MTLBuffer>)m_buffers[m_slot] contents], data, size);
    }
}

void MetalIndexBuffer::terminate() {
    for (uint32_t i = 0; i < METAL_DYNAMIC_BUFFER_RING; i++) {
        safeRelease(m_buffers[i]);
        m_capacities[i] = 0;
    }
    m_slot = 0;
}

// ----- MetalTexture ----------------------------------------------------------

MetalTexture::MetalTexture()
    : m_create()
    , m_texture(nullptr)
    , m_sampler(nullptr) {}

bool MetalTexture::isDepth() const {
    return m_create.isDepthFormat();
}

bool MetalTexture::hasStencil() const {
    return m_create.m_format == DEPTH24STENCIL8;
}

uint32_t MetalTexture::getPixelFormat() const {
    return static_cast<uint32_t>(pixelFormat(m_create.m_format));
}

void MetalTexture::create(void *device, TextureCreate &create) {
    m_create = create;
    m_create.m_data = Foundation::Memory(); // pixel data is consumed here, not stored
    m_sampler = (void *)acquireSampler((id<MTLDevice>)device, m_create);
    createTexture(device, create.m_data);
    create.m_data.release();
}

void MetalTexture::createTexture(void *devicePtr, Foundation::Memory data) {
    id<MTLDevice> device = (id<MTLDevice>)devicePtr;
    const MTLPixelFormat format = pixelFormat(m_create.m_format);
    const uint32_t faces = m_create.m_isCubeMap ? 6 : 1;
    const bool volume = m_create.is3D();
    const bool hasData = data.data != nullptr;
    // numMips > 0 allocates the levels even WITHOUT data (render target: mip 0 is drawn,
    // the chain is built later by the generateTextureMipmaps command); immediate generation
    // at creation — only when there is something to fill it with. A volume has no mips.
    const bool wantsMips = m_create.m_numMips > 0 && !isDepth() && !volume;
    const bool generateMips = wantsMips && hasData;

    MTLTextureDescriptor *descriptor = [[MTLTextureDescriptor alloc] init];
    descriptor.textureType =
        volume ? MTLTextureType3D : (m_create.m_isCubeMap ? MTLTextureTypeCube : MTLTextureType2D);
    descriptor.pixelFormat = format;
    descriptor.width = m_create.m_width;
    descriptor.height = m_create.m_height;
    descriptor.depth = volume ? m_create.m_depth : 1;
    // GL parity: numMips is a TRUNCATION of the pyramid (GL_TEXTURE_MAX_LEVEL = numMips → levels
    // 0..numMips), not the full chain. Deeper than that, sub-tiles of atlases start to bleed in the
    // distance (the Neverland terrain: the 0.004 inset is designed for levels 0..4).
    const NSUInteger fullChain =
        1 + static_cast<NSUInteger>(std::floor(std::log2(std::max(m_create.m_width, m_create.m_height))));
    descriptor.mipmapLevelCount =
        wantsMips ? std::min<NSUInteger>(static_cast<NSUInteger>(m_create.m_numMips) + 1, fullChain) : 1;
    if (isDepth()) {
        descriptor.storageMode = MTLStorageModePrivate;
        descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    } else {
#    if defined(PLATFORM_IOS)
        // Unified memory: shared textures with CPU upload through replaceRegion.
        descriptor.storageMode = MTLStorageModeShared;
#    else
        // Managed: CPU uploads through replaceRegion; on Apple silicon it is treated as shared.
        descriptor.storageMode = MTLStorageModeManaged;
#    endif
        // A volume is shader-read only: it is never a render target.
        descriptor.usage = volume ? MTLTextureUsageShaderRead : (MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead);
    }
    m_texture = (void *)[device newTextureWithDescriptor:descriptor];
    [descriptor release];
    if (!m_texture) {
        LOG_ERROR("METAL: Failed to create texture %ux%ux%u", m_create.m_width, m_create.m_height, m_create.m_depth);
        return;
    }

    if (hasData && volume) {
        replaceVolumeRegion(
            (id<MTLTexture>)m_texture, m_create,
            MTLRegionMake3D(0, 0, 0, m_create.m_width, m_create.m_height, m_create.m_depth),
            static_cast<const uint8_t *>(data.data)
        );
        return;
    }
    // Upload initial data per face; RGB8 sources are expanded to RGBA8.
    if (hasData) {
        id<MTLTexture> texture = (id<MTLTexture>)m_texture;
        const size_t texelCount = static_cast<size_t>(m_create.m_width) * m_create.m_height;
        const bool expand = m_create.m_format == RGB8;
        const uint32_t rowPitch =
            m_create.m_width * (expand ? 4u : static_cast<uint32_t>(m_create.bytesPerTexel()));
        const size_t faceSize = texelCount * m_create.bytesPerTexel();
        const MTLRegion region = MTLRegionMake2D(0, 0, m_create.m_width, m_create.m_height);
        for (uint32_t face = 0; face < faces; face++) {
            const uint8_t *faceData = static_cast<const uint8_t *>(data.data) + face * faceSize;
            std::vector<uint8_t> expanded;
            if (expand) {
                expanded = expandRgbToRgba(faceData, texelCount);
                faceData = expanded.data();
            }
            [texture replaceRegion:region
                       mipmapLevel:0
                             slice:face
                         withBytes:faceData
                       bytesPerRow:rowPitch
                     bytesPerImage:0];
        }
        if (generateMips) {
            // Creation runs on the render thread between frames: a command buffer of its own.
            id<MTLCommandQueue> queue = [device newCommandQueue];
            id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
            id<MTLBlitCommandEncoder> blit = [commandBuffer blitCommandEncoder];
            [blit generateMipmapsForTexture:texture];
            [blit endEncoding];
            [commandBuffer commit];
            [commandBuffer waitUntilCompleted];
            [queue release];
        }
    }
}

void MetalTexture::generateMipmaps(void *queue) {
    id<MTLTexture> texture = (id<MTLTexture>)m_texture;
    if (texture == nil || isDepth() || texture.mipmapLevelCount <= 1) { return; }
    @autoreleasepool {
        id<MTLCommandBuffer> commandBuffer = [(id<MTLCommandQueue>)queue commandBuffer];
        id<MTLBlitCommandEncoder> blit = [commandBuffer blitCommandEncoder];
        [blit generateMipmapsForTexture:texture];
        [blit endEncoding];
        [commandBuffer commit];
    }
}

void MetalTexture::releaseTexture() {
    safeRelease(m_texture);
}

void MetalTexture::update(void *device, Foundation::Memory mem) {
    (void)device;
    if (!m_texture) {
        mem.release();
        return;
    }
    if (m_create.is3D()) {
        replaceVolumeRegion(
            (id<MTLTexture>)m_texture, m_create,
            MTLRegionMake3D(0, 0, 0, m_create.m_width, m_create.m_height, m_create.m_depth),
            static_cast<const uint8_t *>(mem.data)
        );
        mem.release();
        return;
    }
    const size_t texelCount = static_cast<size_t>(m_create.m_width) * m_create.m_height;
    const bool expand = m_create.m_format == RGB8;
    const uint8_t *pixels = static_cast<const uint8_t *>(mem.data);
    std::vector<uint8_t> expanded;
    if (expand) {
        expanded = expandRgbToRgba(pixels, texelCount);
        pixels = expanded.data();
    }
    const uint32_t rowPitch = m_create.m_width * (expand ? 4u : static_cast<uint32_t>(m_create.bytesPerTexel()));
    [(id<MTLTexture>)m_texture replaceRegion:MTLRegionMake2D(0, 0, m_create.m_width, m_create.m_height)
                                 mipmapLevel:0
                                       slice:0
                                   withBytes:pixels
                                 bytesPerRow:rowPitch
                               bytesPerImage:0];
    mem.release();
}

void MetalTexture::updateRegion(
    void *device, uint16_t x, uint16_t y, uint16_t z, uint16_t width, uint16_t height, uint16_t depth,
    Foundation::Memory mem
) {
    (void)device;
    if (!m_texture) {
        mem.release();
        return;
    }
    if (m_create.is3D()) {
        replaceVolumeRegion(
            (id<MTLTexture>)m_texture, m_create, MTLRegionMake3D(x, y, z, width, height, depth),
            static_cast<const uint8_t *>(mem.data)
        );
        mem.release();
        return;
    }
    const size_t texelCount = static_cast<size_t>(width) * height;
    const bool expand = m_create.m_format == RGB8;
    const uint8_t *pixels = static_cast<const uint8_t *>(mem.data);
    std::vector<uint8_t> expanded;
    if (expand) {
        expanded = expandRgbToRgba(pixels, texelCount);
        pixels = expanded.data();
    }
    const uint32_t rowPitch =
        width * (expand ? 4u : static_cast<uint32_t>(m_create.bytesPerTexel()));
    [(id<MTLTexture>)m_texture replaceRegion:MTLRegionMake2D(x, y, width, height)
                                 mipmapLevel:0
                                       slice:0
                                   withBytes:pixels
                                 bytesPerRow:rowPitch
                               bytesPerImage:0];
    mem.release();
}

void MetalTexture::resize(void *device, uint32_t width, uint32_t height) {
    if (m_create.is3D()) {
        LOG_ERROR("METAL: 3D texture resize is not supported");
        return;
    }
    if (m_create.m_width == width && m_create.m_height == height) { return; }
    m_create.m_width = static_cast<uint16_t>(width);
    m_create.m_height = static_cast<uint16_t>(height);
    releaseTexture();
    createTexture(device, Foundation::Memory(nullptr));
}

void MetalTexture::terminate() {
    releaseTexture();
    m_sampler = nullptr; // cached globally
    m_create = TextureCreate();
}

// ----- MetalFrameBuffer ------------------------------------------------------

MetalFrameBuffer::MetalFrameBuffer()
    : m_specification()
    , m_isCreated(false) {}

void MetalFrameBuffer::create(FrameBufferSpecification specification) {
    m_specification = specification;
    m_isCreated = true;
}

void MetalFrameBuffer::terminate() {
    m_specification = FrameBufferSpecification();
    m_isCreated = false;
}

} // namespace Miren

#endif
