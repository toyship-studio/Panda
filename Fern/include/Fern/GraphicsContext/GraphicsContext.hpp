//
// Created by Admin on 18.02.2022.
//

#pragma once

#include "Fern/Base/Base.hpp"

#include <cstdint>

namespace Fern {
class GraphicsContext {
public:
    virtual ~GraphicsContext() = default;
    virtual void lockDrawingContext() {}
    virtual void unlockDrawingContext() {}
    // Activates the drawing context.
    virtual void setCurrent() = 0;
    // Release the drawing context of the calling thread.
    virtual void releaseDrawingContext() = 0;
    virtual void swapBuffers() = 0;
    virtual void update() = 0;
    virtual uint32_t getDefaultFrameBufferId() = 0;
    // Native handles of explicit-API backends (D3D11/Metal); GL backends keep the
    // defaults. The active API is a build-time constant (Foundation::GraphicsAPI).
    // Metal mapping: nativeDevice=MTLDevice, nativeDeviceContext=MTLCommandQueue,
    // nativeBackBufferRTV=current drawable texture (acquires the frame's drawable),
    // nativeDepthStencilView=context-owned depth texture.
    virtual void *nativeDevice() {
        return nullptr;
    }
    virtual void *nativeDeviceContext() {
        return nullptr;
    }
    virtual void *nativeBackBufferRTV() {
        return nullptr;
    }
    virtual void *nativeDepthStencilView() {
        return nullptr;
    }
    // Metal only: the drawable already acquired this frame (nullptr if none). Never
    // acquires by itself — the renderer presents only drawables it actually rendered to.
    virtual void *nativeCurrentDrawable() {
        return nullptr;
    }
};
} // namespace Fern
