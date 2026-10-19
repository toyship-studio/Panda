#pragma once

#include "Fern/GraphicsContext/GraphicsContext.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef UNICODE
#    define UNICODE
#endif
#include <windows.h>

#include <d3d11.h>

#include <atomic>

namespace Fern {

// One D3D11 device is shared by the whole process (resources are visible to all
// windows by construction); every window owns its swap chain and views. All
// rendering calls happen on Miren's render thread; update() may be called from
// the main thread at any frequency and only flags a resize when the client area
// really changed. The pending resize is applied on the render thread before the
// back buffer views are handed out, never after the frame was rendered.
class D3D11_Context final : public GraphicsContext {
public:
    D3D11_Context(HWND window);
    ~D3D11_Context() override;

    void setCurrent() override {}
    void releaseDrawingContext() override {}
    void swapBuffers() override;
    void update() override;
    uint32_t getDefaultFrameBufferId() override {
        return 0;
    }

    void *nativeDevice() override;
    void *nativeDeviceContext() override;
    void *nativeBackBufferRTV() override {
        applyPendingResize();
        return m_backBufferView;
    }
    void *nativeDepthStencilView() override {
        applyPendingResize();
        return m_depthStencilView;
    }

private:
    void applyPendingResize();
    void createSizeDependentResources();
    void releaseSizeDependentResources();

    HWND m_window;
    IDXGISwapChain *m_swapChain;
    ID3D11RenderTargetView *m_backBufferView;
    ID3D11Texture2D *m_depthStencilTexture;
    ID3D11DepthStencilView *m_depthStencilView;
    std::atomic<bool> m_resizePending;
    // Size the swap chain buffers currently have; written on the render thread,
    // compared against the live client rect in update() on the main thread.
    std::atomic<uint32_t> m_bufferWidth;
    std::atomic<uint32_t> m_bufferHeight;
};

} // namespace Fern
