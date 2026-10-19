#pragma once

#import "Fern/GraphicsContext/GraphicsContext.hpp"
#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

// Metal context of a window. There is one device and one command queue per process (Miren
// resources are visible from all windows without GL sharing); the window owns the CAMetalLayer,
// the depth texture and the drawable of the current frame. The drawable is acquired lazily on the
// renderer's first access to the backbuffer and released in swapBuffers (the present is done by
// the renderer itself).
class OSX_MetalContext final : public Fern::GraphicsContext {
public:
    OSX_MetalContext(NSView *view);
    ~OSX_MetalContext() override;
    void setCurrent() override {}
    void releaseDrawingContext() override {}
    void swapBuffers() override;
    void update() override;
    uint32_t getDefaultFrameBufferId() override;
    void *nativeDevice() override;
    void *nativeDeviceContext() override;
    void *nativeBackBufferRTV() override;
    void *nativeDepthStencilView() override;
    void *nativeCurrentDrawable() override;

private:
    void acquireDrawableIfNeeded();
    void ensureDepthTexture(id<MTLTexture> drawableTexture);

    NSView *m_view; // not owned: the view lives in the window
    CAMetalLayer *m_layer;
    id<CAMetalDrawable> m_drawable;
    id<MTLTexture> m_depthTexture;
};
