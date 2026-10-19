#pragma once

#import "Fern/GraphicsContext/GraphicsContext.hpp"
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

// Metal context of an iOS window: a mirror of OSX_MetalContext. The layer is provided by the view
// via +layerClass (WonderView under PANDA_GRAPHICS_METAL). The frame's drawable is acquired lazily
// on the renderer's first access to the backbuffer and released in swapBuffers.
class IOS_MetalContext final : public Fern::GraphicsContext {
public:
    IOS_MetalContext(CAMetalLayer *layer);
    ~IOS_MetalContext() override;
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

    CAMetalLayer *m_layer; // not owned: the backing layer lives in the view
    id<CAMetalDrawable> m_drawable;
    id<MTLTexture> m_depthTexture;
};
