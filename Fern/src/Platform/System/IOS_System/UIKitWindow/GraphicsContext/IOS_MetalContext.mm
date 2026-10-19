#import "IOS_MetalContext.h"

#import <Foundation/Logger.hpp>
#import <UIKit/UIKit.h>

static id<MTLDevice> s_device = nil;
static id<MTLCommandQueue> s_queue = nil;

IOS_MetalContext::IOS_MetalContext(CAMetalLayer *layer)
    : m_layer(layer)
    , m_drawable(nil)
    , m_depthTexture(nil) {
    if (s_device == nil) {
        s_device = MTLCreateSystemDefaultDevice();
        if (s_device == nil) {
            LOG_CRITICAL("ERROR: Failed to create Metal device");
            return;
        }
        s_queue = [s_device newCommandQueue];
    }
    if (m_layer == nil) {
        LOG_CRITICAL("ERROR: View backing layer is not CAMetalLayer");
        return;
    }
    m_layer.device = s_device;
    m_layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    m_layer.framebufferOnly = YES;
    update();
}

IOS_MetalContext::~IOS_MetalContext() {
    [m_drawable release];
    m_drawable = nil;
    [m_depthTexture release];
    m_depthTexture = nil;
    m_layer = nil;
}

void IOS_MetalContext::update() {
    if (m_layer == nil) { return; }
    if (![NSThread isMainThread]) { return; }
    // Render in native pixels (parity with GLES: renderbufferStorage takes bounds×scale).
    const CGFloat scale = m_layer.contentsScale > 0 ? m_layer.contentsScale : UIScreen.mainScreen.nativeScale;
    const CGSize bounds = m_layer.bounds.size;
    const CGSize drawableSize = CGSizeMake(bounds.width * scale, bounds.height * scale);
    if (drawableSize.width < 1 || drawableSize.height < 1) { return; }
    if (!CGSizeEqualToSize(m_layer.drawableSize, drawableSize)) { m_layer.drawableSize = drawableSize; }
}

void IOS_MetalContext::acquireDrawableIfNeeded() {
    if (m_drawable != nil || m_layer == nil) { return; }
    m_drawable = [[m_layer nextDrawable] retain];
}

void IOS_MetalContext::ensureDepthTexture(id<MTLTexture> drawableTexture) {
    if (drawableTexture == nil) { return; }
    if (m_depthTexture != nil && m_depthTexture.width == drawableTexture.width &&
        m_depthTexture.height == drawableTexture.height) {
        return;
    }
    [m_depthTexture release];
    m_depthTexture = nil;
    MTLTextureDescriptor *descriptor =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float_Stencil8
                                                           width:drawableTexture.width
                                                          height:drawableTexture.height
                                                       mipmapped:NO];
    descriptor.storageMode = MTLStorageModePrivate;
    descriptor.usage = MTLTextureUsageRenderTarget;
    m_depthTexture = [s_device newTextureWithDescriptor:descriptor];
}

void IOS_MetalContext::swapBuffers() {
    // Present is already scheduled by the renderer (presentDrawable) — release the frame drawable.
    [m_drawable release];
    m_drawable = nil;
}

uint32_t IOS_MetalContext::getDefaultFrameBufferId() {
    return 0;
}

void *IOS_MetalContext::nativeDevice() {
    return (void *)s_device;
}

void *IOS_MetalContext::nativeDeviceContext() {
    return (void *)s_queue;
}

void *IOS_MetalContext::nativeBackBufferRTV() {
    acquireDrawableIfNeeded();
    return m_drawable != nil ? (void *)m_drawable.texture : nullptr;
}

void *IOS_MetalContext::nativeDepthStencilView() {
    acquireDrawableIfNeeded();
    if (m_drawable == nil) { return nullptr; }
    ensureDepthTexture(m_drawable.texture);
    return (void *)m_depthTexture;
}

void *IOS_MetalContext::nativeCurrentDrawable() {
    return (void *)m_drawable;
}
