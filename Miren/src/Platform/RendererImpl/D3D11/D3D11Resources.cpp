#include "D3D11Resources.hpp"
#include "RendererD3D11.hpp"

#ifdef PLATFORM_WINDOWS

#    define WIN32_LEAN_AND_MEAN
#    define NOMINMAX
#    ifndef UNICODE
#        define UNICODE
#    endif
#    include <windows.h>
#    include <d3d11.h>

#    include <algorithm>
#    include <cstring>
#    include <map>
#    include <vector>

namespace Miren {

namespace {

    template<typename T>
    void safeRelease(T *&object) {
        if (object) {
            object->Release();
            object = nullptr;
        }
    }

    ID3D11Buffer *createBuffer(ID3D11Device *device, UINT bindFlags, const void *data, uint32_t size, bool isDynamic) {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = size;
        desc.BindFlags = bindFlags;
        if (isDynamic) {
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        } else {
            desc.Usage = D3D11_USAGE_IMMUTABLE;
        }
        D3D11_SUBRESOURCE_DATA initial = {};
        initial.pSysMem = data;
        ID3D11Buffer *buffer = nullptr;
        if (FAILED(device->CreateBuffer(&desc, data ? &initial : nullptr, &buffer))) {
            LOG_ERROR("D3D11: Failed to create buffer (%u bytes)", size);
            return nullptr;
        }
        return buffer;
    }

    void updateDynamicBuffer(ID3D11DeviceContext *deviceContext, ID3D11Buffer *buffer, const void *data, uint32_t size) {
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (SUCCEEDED(deviceContext->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            std::memcpy(mapped.pData, data, size);
            deviceContext->Unmap(buffer, 0);
        }
    }

    struct FormatInfo {
        DXGI_FORMAT resource;
        DXGI_FORMAT shaderView;
        DXGI_FORMAT depthView;
    };

    FormatInfo formatInfo(TextureFormat format) {
        switch (format) {
            case R8:
                return {DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_UNKNOWN};
            case R8I:
                return {DXGI_FORMAT_R8_SINT, DXGI_FORMAT_R8_SINT, DXGI_FORMAT_UNKNOWN};
            case R8UI:
                return {DXGI_FORMAT_R8_UINT, DXGI_FORMAT_R8_UINT, DXGI_FORMAT_UNKNOWN};
            case RGB8: // expanded to RGBA8 at upload, D3D11 has no 24-bit format
            case RGBA8:
                return {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_UNKNOWN};
            case RGBA8_SRGB: // hardware sRGB→linear decode on sample
                return {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_UNKNOWN};
            case R32I:
                return {DXGI_FORMAT_R32_SINT, DXGI_FORMAT_R32_SINT, DXGI_FORMAT_UNKNOWN};
            case R32UI:
                return {DXGI_FORMAT_R32_UINT, DXGI_FORMAT_R32_UINT, DXGI_FORMAT_UNKNOWN};
            case RGBA16F:
                return {DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_UNKNOWN};
            case DEPTH24STENCIL8:
                return {DXGI_FORMAT_R24G8_TYPELESS, DXGI_FORMAT_R24_UNORM_X8_TYPELESS, DXGI_FORMAT_D24_UNORM_S8_UINT};
            case DEPTH32F:
                return {DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_D32_FLOAT};
            case RG8:
                return {DXGI_FORMAT_R8G8_UNORM, DXGI_FORMAT_R8G8_UNORM, DXGI_FORMAT_UNKNOWN};
        }
        return {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN};
    }

    D3D11_FILTER samplerFilter(TextureFiltering minFilter, TextureFiltering magFilter) {
        const bool minLinear = minFilter != NEAREST && minFilter != NEAREST_MIPMAP_NEAREST &&
                               minFilter != NEAREST_MIPMAP_LINEAR;
        const bool magLinear = magFilter != NEAREST;
        const bool mipLinear = minFilter == NEAREST_MIPMAP_LINEAR || minFilter == LINEAR_MIPMAP_LINEAR ||
                               minFilter == LINEAR || minFilter == NEAREST;
        if (minLinear && magLinear) {
            return mipLinear ? D3D11_FILTER_MIN_MAG_MIP_LINEAR : D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        }
        if (!minLinear && !magLinear) {
            return mipLinear ? D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR : D3D11_FILTER_MIN_MAG_MIP_POINT;
        }
        if (minLinear) { return D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT; }
        return D3D11_FILTER_MIN_POINT_MAG_MIP_LINEAR;
    }

    D3D11_TEXTURE_ADDRESS_MODE addressMode(TextureWrapMode wrap) {
        switch (wrap) {
            case CLAMP:
                return D3D11_TEXTURE_ADDRESS_CLAMP;
            case REPEAT:
                return D3D11_TEXTURE_ADDRESS_WRAP;
            case CLAMP_TO_BORDER:
                return D3D11_TEXTURE_ADDRESS_BORDER;
        }
        return D3D11_TEXTURE_ADDRESS_WRAP;
    }

    // Sampler states are immutable and D3D11 caps their total count; a handful of
    // filter/wrap combinations exist, so they are cached for the process lifetime.
    ID3D11SamplerState *acquireSampler(ID3D11Device *device, const TextureCreate &create) {
        D3D11_FILTER filter = samplerFilter(create.m_minFiltering, create.m_magFiltering);
        const D3D11_TEXTURE_ADDRESS_MODE address = addressMode(create.m_wrap);
        const bool compare = create.m_compare == COMPARE_LESS_EQUAL;
        // Comparison sampler (shadow map, SampleCmp): the same filters in the COMPARISON variant.
        if (compare) { filter = static_cast<D3D11_FILTER>(filter | D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT); }
        static std::map<std::pair<int, int>, ID3D11SamplerState *> s_samplers;
        const std::pair<int, int> key(static_cast<int>(filter), static_cast<int>(address));
        auto it = s_samplers.find(key);
        if (it != s_samplers.end()) { return it->second; }
        D3D11_SAMPLER_DESC desc = {};
        desc.Filter = filter;
        desc.AddressU = address;
        desc.AddressV = address;
        desc.AddressW = address;
        // The border is white: the shadow map beyond the edge reads as "lit".
        desc.BorderColor[0] = desc.BorderColor[1] = desc.BorderColor[2] = desc.BorderColor[3] = 1.f;
        desc.ComparisonFunc = compare ? D3D11_COMPARISON_LESS_EQUAL : D3D11_COMPARISON_NEVER;
        desc.MaxLOD = D3D11_FLOAT32_MAX;
        ID3D11SamplerState *sampler = nullptr;
        if (FAILED(device->CreateSamplerState(&desc, &sampler))) {
            LOG_ERROR("D3D11: Failed to create sampler state");
            return nullptr;
        }
        s_samplers[key] = sampler;
        return sampler;
    }

    // D3D11 has no 24-bit RGB format; expand to RGBA with opaque alpha.
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

} // namespace

// ----- D3D11VertexBuffer ---------------------------------------------------

D3D11VertexBuffer::D3D11VertexBuffer()
    : m_buffer(nullptr)
    , m_layoutHandle(MIREN_INVALID_HANDLE)
    , m_capacity(0)
    , m_isDynamic(false) {}

void D3D11VertexBuffer::create(
    ID3D11Device *device, Foundation::Memory data, uint32_t size, VertexLayoutHandle layout, bool isDynamic
) {
    m_layoutHandle = layout;
    m_isDynamic = isDynamic;
    m_capacity = size;
    m_buffer = createBuffer(device, D3D11_BIND_VERTEX_BUFFER, data.data, size, isDynamic);
    data.release();
}

void D3D11VertexBuffer::update(
    ID3D11Device *device, ID3D11DeviceContext *deviceContext, const void *data, uint32_t size
) {
    if (!m_isDynamic) {
        LOG_ERROR("D3D11: Attempt to update a static vertex buffer");
        return;
    }
    if (size > m_capacity || !m_buffer) {
        safeRelease(m_buffer);
        m_capacity = std::max(size, m_capacity * 2);
        m_buffer = createBuffer(device, D3D11_BIND_VERTEX_BUFFER, nullptr, m_capacity, true);
    }
    if (m_buffer && size > 0) { updateDynamicBuffer(deviceContext, m_buffer, data, size); }
}

void D3D11VertexBuffer::terminate() {
    safeRelease(m_buffer);
    m_layoutHandle = VertexLayoutHandle();
    m_capacity = 0;
}

// ----- D3D11IndexBuffer ----------------------------------------------------

D3D11IndexBuffer::D3D11IndexBuffer()
    : m_buffer(nullptr)
    , m_elementType(BufferElementType::UnsignedInt)
    , m_capacityBytes(0)
    , m_isDynamic(false) {}

uint32_t D3D11IndexBuffer::getIndexSize() const {
    return m_elementType == BufferElementType::UnsignedShort ? 2 : 4;
}

uint32_t D3D11IndexBuffer::getFormat() const {
    return m_elementType == BufferElementType::UnsignedShort ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
}

void D3D11IndexBuffer::create(
    ID3D11Device *device, Foundation::Memory data, BufferElementType elementType, size_t count, bool isDynamic
) {
    m_elementType = elementType;
    m_isDynamic = isDynamic;
    const uint32_t size = static_cast<uint32_t>(count * getIndexSize());
    m_capacityBytes = size;
    m_buffer = createBuffer(device, D3D11_BIND_INDEX_BUFFER, data.data, size, isDynamic);
    data.release();
}

void D3D11IndexBuffer::update(
    ID3D11Device *device, ID3D11DeviceContext *deviceContext, const void *data, size_t count
) {
    if (!m_isDynamic) {
        LOG_ERROR("D3D11: Attempt to update a static index buffer");
        return;
    }
    const uint32_t size = static_cast<uint32_t>(count * getIndexSize());
    if (size > m_capacityBytes || !m_buffer) {
        safeRelease(m_buffer);
        m_capacityBytes = std::max(size, m_capacityBytes * 2);
        m_buffer = createBuffer(device, D3D11_BIND_INDEX_BUFFER, nullptr, m_capacityBytes, true);
    }
    if (m_buffer && size > 0) { updateDynamicBuffer(deviceContext, m_buffer, data, size); }
}

void D3D11IndexBuffer::terminate() {
    safeRelease(m_buffer);
    m_capacityBytes = 0;
}

// ----- D3D11Texture ---------------------------------------------------------

D3D11Texture::D3D11Texture()
    : m_create()
    , m_texture(nullptr)
    , m_volume(nullptr)
    , m_shaderResourceView(nullptr)
    , m_renderTargetView(nullptr)
    , m_depthStencilView(nullptr)
    , m_sampler(nullptr) {}

bool D3D11Texture::isDepth() const {
    return m_create.isDepthFormat();
}

bool D3D11Texture::hasStencil() const {
    return m_create.m_format == DEPTH24STENCIL8;
}

void D3D11Texture::create(ID3D11Device *device, ID3D11DeviceContext *deviceContext, TextureCreate &create) {
    m_create = create;
    m_create.m_data = Foundation::Memory(); // pixel data is consumed here, not stored
    m_sampler = acquireSampler(device, m_create);
    if (m_create.is3D()) {
        createVolumeAndView(device, deviceContext, create.m_data);
    } else {
        createTextureAndViews(device, deviceContext, create.m_data);
    }
    create.m_data.release();
}

ID3D11Resource *D3D11Texture::resource() const {
    if (m_volume) { return m_volume; }
    return m_texture;
}

// Volume: a Texture3D without mips and without a render target, SRV TEXTURE3D; the data is
// slices in a row (SrcDepthPitch is the slice pitch), RGB8 is expanded to RGBA8.
void D3D11Texture::createVolumeAndView(
    ID3D11Device *device, ID3D11DeviceContext *deviceContext, Foundation::Memory data
) {
    const FormatInfo info = formatInfo(m_create.m_format);
    D3D11_TEXTURE3D_DESC desc = {};
    desc.Width = m_create.m_width;
    desc.Height = m_create.m_height;
    desc.Depth = m_create.m_depth;
    desc.MipLevels = 1;
    desc.Format = info.resource;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device->CreateTexture3D(&desc, nullptr, &m_volume))) {
        LOG_ERROR("D3D11: Failed to create 3D texture %ux%ux%u", m_create.m_width, m_create.m_height, m_create.m_depth);
        return;
    }
    if (data.data != nullptr) {
        const size_t texelCount = static_cast<size_t>(m_create.m_width) * m_create.m_height * m_create.m_depth;
        const bool expand = m_create.m_format == RGB8;
        const uint8_t *pixels = static_cast<const uint8_t *>(data.data);
        std::vector<uint8_t> expanded;
        if (expand) {
            expanded = expandRgbToRgba(pixels, texelCount);
            pixels = expanded.data();
        }
        const uint32_t rowPitch = m_create.m_width * (expand ? 4u : static_cast<uint32_t>(m_create.bytesPerTexel()));
        deviceContext->UpdateSubresource(m_volume, 0, nullptr, pixels, rowPitch, rowPitch * m_create.m_height);
    }
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = info.shaderView;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
    srvDesc.Texture3D.MostDetailedMip = 0;
    srvDesc.Texture3D.MipLevels = 1;
    if (FAILED(device->CreateShaderResourceView(m_volume, &srvDesc, &m_shaderResourceView))) {
        LOG_ERROR("D3D11: Failed to create 3D shader resource view");
    }
}

void D3D11Texture::createTextureAndViews(
    ID3D11Device *device, ID3D11DeviceContext *deviceContext, Foundation::Memory data
) {
    const FormatInfo info = formatInfo(m_create.m_format);
    const uint32_t faces = m_create.m_isCubeMap ? 6 : 1;
    const bool hasData = data.data != nullptr;
    // numMips > 0 allocates the levels even WITHOUT data (render target: mip 0 is drawn, the chain
    // is built later by generateTextureMipmaps); generation at creation — only with data.
    const bool wantsMips = m_create.m_numMips > 0 && !isDepth();
    const bool generateMips = wantsMips && hasData;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = m_create.m_width;
    desc.Height = m_create.m_height;
    desc.MipLevels = wantsMips ? 0 : 1;
    desc.ArraySize = faces;
    desc.Format = info.resource;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (isDepth()) {
        desc.BindFlags |= D3D11_BIND_DEPTH_STENCIL;
    } else {
        // Render-target binding also covers mip generation requirements.
        desc.BindFlags |= D3D11_BIND_RENDER_TARGET;
    }
    if (m_create.m_isCubeMap) { desc.MiscFlags |= D3D11_RESOURCE_MISC_TEXTURECUBE; }
    if (wantsMips) { desc.MiscFlags |= D3D11_RESOURCE_MISC_GENERATE_MIPS; }

    if (FAILED(device->CreateTexture2D(&desc, nullptr, &m_texture))) {
        LOG_ERROR("D3D11: Failed to create texture %ux%u", m_create.m_width, m_create.m_height);
        return;
    }
    D3D11_TEXTURE2D_DESC createdDesc = {};
    m_texture->GetDesc(&createdDesc); // MipLevels=0 above resolves to the full chain

    // Upload initial data per face; RGB8 sources are expanded to RGBA8.
    if (hasData) {
        const size_t texelCount = static_cast<size_t>(m_create.m_width) * m_create.m_height;
        const bool expand = m_create.m_format == RGB8;
        const uint32_t rowPitch = m_create.m_width * (expand ? 4u : static_cast<uint32_t>(m_create.bytesPerTexel()));
        const size_t faceSize = texelCount * m_create.bytesPerTexel();
        for (uint32_t face = 0; face < faces; face++) {
            const uint8_t *faceData = static_cast<const uint8_t *>(data.data) + face * faceSize;
            std::vector<uint8_t> expanded;
            if (expand) {
                expanded = expandRgbToRgba(faceData, texelCount);
                faceData = expanded.data();
            }
            const UINT subresource = D3D11CalcSubresource(0, face, createdDesc.MipLevels);
            deviceContext->UpdateSubresource(m_texture, subresource, nullptr, faceData, rowPitch, 0);
        }
    }

    if (isDepth()) {
        D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format = info.depthView;
        dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        if (FAILED(device->CreateDepthStencilView(m_texture, &dsvDesc, &m_depthStencilView))) {
            LOG_ERROR("D3D11: Failed to create depth stencil view");
        }
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = info.shaderView;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        if (FAILED(device->CreateShaderResourceView(m_texture, &srvDesc, &m_shaderResourceView))) {
            LOG_ERROR("D3D11: Failed to create depth shader view");
        }
        return;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = info.shaderView;
    if (m_create.m_isCubeMap) {
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.TextureCube.MipLevels = wantsMips ? static_cast<UINT>(-1) : 1;
    } else {
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = wantsMips ? static_cast<UINT>(-1) : 1;
    }
    if (FAILED(device->CreateShaderResourceView(m_texture, &srvDesc, &m_shaderResourceView))) {
        LOG_ERROR("D3D11: Failed to create shader resource view");
    }
    if (!m_create.m_isCubeMap) {
        if (FAILED(device->CreateRenderTargetView(m_texture, nullptr, &m_renderTargetView))) {
            LOG_ERROR("D3D11: Failed to create render target view");
        }
    }
    if (generateMips && m_shaderResourceView) { deviceContext->GenerateMips(m_shaderResourceView); }
}

void D3D11Texture::releaseTextureAndViews() {
    safeRelease(m_depthStencilView);
    safeRelease(m_renderTargetView);
    safeRelease(m_shaderResourceView);
    safeRelease(m_texture);
    safeRelease(m_volume);
}

void D3D11Texture::update(ID3D11Device *device, ID3D11DeviceContext *deviceContext, Foundation::Memory mem) {
    ID3D11Resource *target = resource();
    if (!target) {
        mem.release();
        return;
    }
    const uint32_t depth = m_create.is3D() ? m_create.m_depth : 1u;
    const size_t texelCount = static_cast<size_t>(m_create.m_width) * m_create.m_height * depth;
    const bool expand = m_create.m_format == RGB8;
    const uint8_t *pixels = static_cast<const uint8_t *>(mem.data);
    std::vector<uint8_t> expanded;
    if (expand) {
        expanded = expandRgbToRgba(pixels, texelCount);
        pixels = expanded.data();
    }
    const uint32_t rowPitch = m_create.m_width * (expand ? 4u : static_cast<uint32_t>(m_create.bytesPerTexel()));
    deviceContext->UpdateSubresource(target, 0, nullptr, pixels, rowPitch, rowPitch * m_create.m_height);
    mem.release();
}

void D3D11Texture::updateRegion(
    ID3D11Device *device, ID3D11DeviceContext *deviceContext, uint16_t x, uint16_t y, uint16_t z,
    uint16_t width, uint16_t height, uint16_t depth, Foundation::Memory mem
) {
    (void)device;
    ID3D11Resource *target = resource();
    if (!target) {
        mem.release();
        return;
    }
    const size_t texelCount = static_cast<size_t>(width) * height * depth;
    const bool expand = m_create.m_format == RGB8;
    const uint8_t *pixels = static_cast<const uint8_t *>(mem.data);
    std::vector<uint8_t> expanded;
    if (expand) {
        expanded = expandRgbToRgba(pixels, texelCount);
        pixels = expanded.data();
    }
    const uint32_t rowPitch =
        width * (expand ? 4u : static_cast<uint32_t>(m_create.bytesPerTexel()));
    D3D11_BOX box;
    box.left = x;
    box.top = y;
    box.front = z;
    box.right = static_cast<UINT>(x) + width;
    box.bottom = static_cast<UINT>(y) + height;
    box.back = static_cast<UINT>(z) + depth;
    deviceContext->UpdateSubresource(target, 0, &box, pixels, rowPitch, rowPitch * height);
    mem.release();
}

void D3D11Texture::resize(ID3D11Device *device, ID3D11DeviceContext *deviceContext, uint32_t width, uint32_t height) {
    if (m_create.is3D()) {
        LOG_ERROR("D3D11: 3D texture resize is not supported");
        return;
    }
    if (m_create.m_width == width && m_create.m_height == height) { return; }
    m_create.m_width = static_cast<uint16_t>(width);
    m_create.m_height = static_cast<uint16_t>(height);
    releaseTextureAndViews();
    createTextureAndViews(device, deviceContext, Foundation::Memory(nullptr));
}

void D3D11Texture::generateMipmaps(ID3D11DeviceContext *deviceContext) {
    if (m_shaderResourceView == nullptr || m_create.m_numMips == 0 || isDepth() || m_create.is3D()) { return; }
    deviceContext->GenerateMips(m_shaderResourceView);
}

void D3D11Texture::terminate() {
    releaseTextureAndViews();
    m_sampler = nullptr; // cached globally
    m_create = TextureCreate();
}

// ----- D3D11FrameBuffer -----------------------------------------------------

D3D11FrameBuffer::D3D11FrameBuffer()
    : m_specification()
    , m_isCreated(false) {}

void D3D11FrameBuffer::create(RendererD3D11 *renderer, ID3D11Device *device, FrameBufferSpecification specification) {
    m_specification = specification;
    m_isCreated = true;
}

void D3D11FrameBuffer::terminate() {
    m_specification = FrameBufferSpecification();
    m_isCreated = false;
}

void D3D11FrameBuffer::bind(RendererD3D11 *renderer, ID3D11DeviceContext *deviceContext) {
    ID3D11RenderTargetView *colorViews[MAX_FRAMEBUFFER_ATTACHMENTS] = {};
    ID3D11DepthStencilView *depthView = nullptr;
    uint32_t colorCount = 0;
    for (uint32_t i = 0; i < m_specification.num; i++) {
        D3D11Texture &texture = renderer->getTexture(m_specification.attachments[i].handle);
        if (texture.isDepth()) {
            depthView = texture.getDepthStencilView();
        } else {
            colorViews[colorCount++] = texture.getRenderTargetView();
        }
    }
    deviceContext->OMSetRenderTargets(colorCount, colorViews, depthView);
}

void D3D11FrameBuffer::clearDepth(RendererD3D11 *renderer, ID3D11DeviceContext *deviceContext) {
    for (uint32_t i = 0; i < m_specification.num; i++) {
        D3D11Texture &texture = renderer->getTexture(m_specification.attachments[i].handle);
        if (texture.isDepth() && texture.getDepthStencilView()) {
            const UINT flags = D3D11_CLEAR_DEPTH | (texture.hasStencil() ? D3D11_CLEAR_STENCIL : 0);
            deviceContext->ClearDepthStencilView(texture.getDepthStencilView(), flags, 1.f, 0);
        }
    }
}

void D3D11FrameBuffer::clearUIntAttachment(
    RendererD3D11 *renderer, ID3D11DeviceContext *deviceContext, int index, uint32_t value
) {
    if (index < 0 || static_cast<uint32_t>(index) >= m_specification.num) { return; }
    D3D11Texture &texture = renderer->getTexture(m_specification.attachments[index].handle);
    if (!texture.getRenderTargetView()) { return; }
    // Note: ClearRenderTargetView converts float→uint for integer targets; exact
    // above 2^24 is not guaranteed, but clear values are small sentinels (0).
    const float clearValue[4] = {
        static_cast<float>(value), static_cast<float>(value), static_cast<float>(value), static_cast<float>(value)
    };
    deviceContext->ClearRenderTargetView(texture.getRenderTargetView(), clearValue);
}

void D3D11FrameBuffer::readPixels(
    RendererD3D11 *renderer,
    ID3D11Device *device,
    ID3D11DeviceContext *deviceContext,
    int index,
    int x,
    int y,
    int width,
    int height,
    void *data
) {
    if (index < 0 || static_cast<uint32_t>(index) >= m_specification.num) { return; }
    D3D11Texture &texture = renderer->getTexture(m_specification.attachments[index].handle);
    if (!texture.getTexture()) { return; }

    const FormatInfo info = formatInfo(texture.getFormat());

    // Callers use GL conventions: the rect origin is bottom-left. An
    // out-of-bounds CopySubresourceRegion is undefined behavior in D3D11 (GL
    // clamps silently), so the region is clipped to the texture explicitly.
    const int texWidth = texture.getWidth();
    const int texHeight = texture.getHeight();
    const int top = texHeight - y - height;
    const int clampedLeft = std::max(0, x);
    const int clampedTop = std::max(0, top);
    const int clampedRight = std::min(texWidth, x + width);
    const int clampedBottom = std::min(texHeight, top + height);
    if (clampedLeft >= clampedRight || clampedTop >= clampedBottom) { return; }
    if (clampedLeft != x || clampedTop != top || clampedRight != x + width || clampedBottom != top + height) {
        LOG_ERROR("D3D11: readPixels rect is out of texture bounds, clipped");
    }
    const int readWidth = clampedRight - clampedLeft;
    const int readHeight = clampedBottom - clampedTop;

    D3D11_TEXTURE2D_DESC stagingDesc = {};
    stagingDesc.Width = static_cast<UINT>(readWidth);
    stagingDesc.Height = static_cast<UINT>(readHeight);
    stagingDesc.MipLevels = 1;
    stagingDesc.ArraySize = 1;
    stagingDesc.Format = info.resource;
    stagingDesc.SampleDesc.Count = 1;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D *staging = nullptr;
    if (FAILED(device->CreateTexture2D(&stagingDesc, nullptr, &staging))) {
        LOG_ERROR("D3D11: Failed to create staging texture for readback");
        return;
    }

    D3D11_BOX box = {};
    box.left = static_cast<UINT>(clampedLeft);
    box.top = static_cast<UINT>(clampedTop);
    box.right = static_cast<UINT>(clampedRight);
    box.bottom = static_cast<UINT>(clampedBottom);
    box.back = 1;
    deviceContext->CopySubresourceRegion(staging, 0, 0, 0, 0, texture.getTexture(), 0, &box);

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (SUCCEEDED(deviceContext->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
        TextureCreate createInfo;
        createInfo.m_format = texture.getFormat();
        const size_t rowBytes = static_cast<size_t>(readWidth) * createInfo.bytesPerTexel();
        for (int row = 0; row < readHeight; row++) {
            std::memcpy(
                static_cast<uint8_t *>(data) + static_cast<size_t>(row) * rowBytes,
                static_cast<const uint8_t *>(mapped.pData) + static_cast<size_t>(row) * mapped.RowPitch,
                rowBytes
            );
        }
        deviceContext->Unmap(staging, 0);
    }
    staging->Release();
}

} // namespace Miren

#endif
