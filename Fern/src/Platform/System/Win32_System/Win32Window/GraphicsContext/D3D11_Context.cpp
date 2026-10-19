#include "D3D11_Context.hpp"

#include <Foundation/Logger.hpp>

#ifdef _DEBUG
#    include <d3d11sdklayers.h>
#    include <vector>
#endif

namespace Fern {

namespace {

    // The device and immediate context are process-wide; the first window creates
    // them, every next window reuses them so GPU resources are naturally shared.
    ID3D11Device *s_device = nullptr;
    ID3D11DeviceContext *s_deviceContext = nullptr;

#ifdef _DEBUG
    ID3D11InfoQueue *s_infoQueue = nullptr;

    void drainDebugMessages() {
        if (!s_infoQueue) { return; }
        const UINT64 count = s_infoQueue->GetNumStoredMessages();
        std::vector<char> storage;
        for (UINT64 i = 0; i < count; i++) {
            SIZE_T length = 0;
            if (FAILED(s_infoQueue->GetMessage(i, nullptr, &length)) || length == 0) { continue; }
            storage.resize(length);
            D3D11_MESSAGE *message = reinterpret_cast<D3D11_MESSAGE *>(storage.data());
            if (FAILED(s_infoQueue->GetMessage(i, message, &length))) { continue; }
            if (message->Severity > D3D11_MESSAGE_SEVERITY_WARNING) { continue; }
            LOG_ERROR("D3D11 debug: %s", message->pDescription);
        }
        s_infoQueue->ClearStoredMessages();
    }
#endif

    bool ensureDeviceCreated() {
        if (s_device) { return true; }
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
        flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
        const D3D_FEATURE_LEVEL requestedLevels[] = {D3D_FEATURE_LEVEL_11_0};
        D3D_FEATURE_LEVEL createdLevel = {};
        HRESULT result = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            flags,
            requestedLevels,
            1,
            D3D11_SDK_VERSION,
            &s_device,
            &createdLevel,
            &s_deviceContext
        );
#ifdef _DEBUG
        if (FAILED(result)) {
            // The debug layer requires the "Graphics Tools" optional feature.
            flags &= ~D3D11_CREATE_DEVICE_DEBUG;
            result = D3D11CreateDevice(
                nullptr,
                D3D_DRIVER_TYPE_HARDWARE,
                nullptr,
                flags,
                requestedLevels,
                1,
                D3D11_SDK_VERSION,
                &s_device,
                &createdLevel,
                &s_deviceContext
            );
        }
#endif
        if (FAILED(result)) {
            LOG_CRITICAL("D3D11: Failed to create device (0x%08lX)", static_cast<unsigned long>(result));
            return false;
        }
#ifdef _DEBUG
        s_device->QueryInterface(__uuidof(ID3D11InfoQueue), reinterpret_cast<void **>(&s_infoQueue));
        LOG_INFO("D3D11: debug layer %s", s_infoQueue ? "active" : "not available");
#endif
        return true;
    }

} // namespace

D3D11_Context::D3D11_Context(HWND window)
    : m_window(window)
    , m_swapChain(nullptr)
    , m_backBufferView(nullptr)
    , m_depthStencilTexture(nullptr)
    , m_depthStencilView(nullptr)
    , m_resizePending(false)
    , m_bufferWidth(0)
    , m_bufferHeight(0) {
    if (!ensureDeviceCreated()) { return; }

    IDXGIDevice *dxgiDevice = nullptr;
    IDXGIAdapter *dxgiAdapter = nullptr;
    IDXGIFactory *dxgiFactory = nullptr;
    if (FAILED(s_device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void **>(&dxgiDevice))) ||
        FAILED(dxgiDevice->GetAdapter(&dxgiAdapter)) ||
        FAILED(dxgiAdapter->GetParent(__uuidof(IDXGIFactory), reinterpret_cast<void **>(&dxgiFactory)))) {
        LOG_CRITICAL("D3D11: Failed to get DXGI factory");
        if (dxgiAdapter) { dxgiAdapter->Release(); }
        if (dxgiDevice) { dxgiDevice->Release(); }
        return;
    }

    DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
    swapChainDesc.BufferCount = 2;
    swapChainDesc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.OutputWindow = m_window;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.Windowed = TRUE;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    if (FAILED(dxgiFactory->CreateSwapChain(s_device, &swapChainDesc, &m_swapChain))) {
        LOG_CRITICAL("D3D11: Failed to create swap chain");
    }
    // Fern handles fullscreen itself (borderless window), never DXGI.
    dxgiFactory->MakeWindowAssociation(m_window, DXGI_MWA_NO_ALT_ENTER);
    dxgiFactory->Release();
    dxgiAdapter->Release();
    dxgiDevice->Release();

    createSizeDependentResources();
}

D3D11_Context::~D3D11_Context() {
    releaseSizeDependentResources();
    if (m_swapChain) { m_swapChain->Release(); }
    // The shared device intentionally stays alive for the process lifetime.
}

void D3D11_Context::createSizeDependentResources() {
    if (!m_swapChain) { return; }
    ID3D11Texture2D *backBuffer = nullptr;
    if (FAILED(m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&backBuffer)))) {
        LOG_CRITICAL("D3D11: Failed to get back buffer");
        return;
    }
    if (FAILED(s_device->CreateRenderTargetView(backBuffer, nullptr, &m_backBufferView))) {
        LOG_CRITICAL("D3D11: Failed to create back buffer view");
    }
    D3D11_TEXTURE2D_DESC backBufferDesc = {};
    backBuffer->GetDesc(&backBufferDesc);
    backBuffer->Release();
    m_bufferWidth = backBufferDesc.Width;
    m_bufferHeight = backBufferDesc.Height;

    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = backBufferDesc.Width;
    depthDesc.Height = backBufferDesc.Height;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (FAILED(s_device->CreateTexture2D(&depthDesc, nullptr, &m_depthStencilTexture)) ||
        FAILED(s_device->CreateDepthStencilView(m_depthStencilTexture, nullptr, &m_depthStencilView))) {
        LOG_CRITICAL("D3D11: Failed to create depth stencil");
    }
}

void D3D11_Context::releaseSizeDependentResources() {
    if (m_depthStencilView) {
        m_depthStencilView->Release();
        m_depthStencilView = nullptr;
    }
    if (m_depthStencilTexture) {
        m_depthStencilTexture->Release();
        m_depthStencilTexture = nullptr;
    }
    if (m_backBufferView) {
        m_backBufferView->Release();
        m_backBufferView = nullptr;
    }
}

void D3D11_Context::applyPendingResize() {
    if (!m_swapChain) { return; }
    if (!m_resizePending.exchange(false)) { return; }
    RECT client = {};
    GetClientRect(m_window, &client);
    const uint32_t width = static_cast<uint32_t>(client.right - client.left);
    const uint32_t height = static_cast<uint32_t>(client.bottom - client.top);
    if (width == 0 || height == 0) { return; }
    if (width == m_bufferWidth && height == m_bufferHeight) { return; }
    s_deviceContext->OMSetRenderTargets(0, nullptr, nullptr);
    releaseSizeDependentResources();
    if (FAILED(m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0))) {
        LOG_ERROR("D3D11: Failed to resize swap chain");
    }
    createSizeDependentResources();
}

void D3D11_Context::swapBuffers() {
    if (!m_swapChain) { return; }
    const HRESULT presentResult = m_swapChain->Present(1, 0);
    if (FAILED(presentResult)) {
        LOG_ERROR("D3D11: Present failed (0x%08lX)", static_cast<unsigned long>(presentResult));
        if (presentResult == DXGI_ERROR_DEVICE_REMOVED && s_device) {
            LOG_ERROR(
                "D3D11: Device removed (0x%08lX)", static_cast<unsigned long>(s_device->GetDeviceRemovedReason())
            );
        }
    }
#ifdef _DEBUG
    drainDebugMessages();
#endif
}

void D3D11_Context::update() {
    // May be called from the main thread at any frequency (PandaUI syncs every
    // frame); only a real client-size change flags the swap chain resize.
    RECT client = {};
    GetClientRect(m_window, &client);
    const uint32_t width = static_cast<uint32_t>(client.right - client.left);
    const uint32_t height = static_cast<uint32_t>(client.bottom - client.top);
    if (width == 0 || height == 0) { return; }
    if (width != m_bufferWidth || height != m_bufferHeight) { m_resizePending = true; }
}

void *D3D11_Context::nativeDevice() {
    return s_device;
}

void *D3D11_Context::nativeDeviceContext() {
    return s_deviceContext;
}

} // namespace Fern
