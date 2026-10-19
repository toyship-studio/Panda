#import "OSX_MetalContext.h"

#import <Foundation/Logger.hpp>

static id<MTLDevice> s_device = nil;
static id<MTLCommandQueue> s_queue = nil;

OSX_MetalContext::OSX_MetalContext(NSView *view)
    : m_view(view)
    , m_layer(nil)
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
    if (![view.layer isKindOfClass:[CAMetalLayer class]]) {
        LOG_CRITICAL("ERROR: Window view backing layer is not CAMetalLayer");
        return;
    }
    m_layer = (CAMetalLayer *)view.layer;
    m_layer.device = s_device;
    m_layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    m_layer.framebufferOnly = YES;
    update();
}

OSX_MetalContext::~OSX_MetalContext() {
    [m_drawable release];
    m_drawable = nil;
    [m_depthTexture release];
    m_depthTexture = nil;
    m_layer = nil;
}

void OSX_MetalContext::update() {
    if (m_layer == nil) { return; }
    if (![NSThread isMainThread]) { return; }
    // Retina parity with the GL path (wantsBestResolutionOpenGLSurface): render in pixels.
    const CGFloat scale = m_view.window ? m_view.window.backingScaleFactor : 1.0;
    if (m_layer.contentsScale != scale) { m_layer.contentsScale = scale; }
    const NSSize size = [m_view convertSizeToBacking:m_view.bounds.size];
    const CGSize drawableSize = CGSizeMake(size.width, size.height);
    if (drawableSize.width < 1 || drawableSize.height < 1) { return; }
    if (!CGSizeEqualToSize(m_layer.drawableSize, drawableSize)) { m_layer.drawableSize = drawableSize; }
}

void OSX_MetalContext::acquireDrawableIfNeeded() {
    if (m_drawable != nil || m_layer == nil) { return; }
    m_drawable = [[m_layer nextDrawable] retain];
}

void OSX_MetalContext::ensureDepthTexture(id<MTLTexture> drawableTexture) {
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

void OSX_MetalContext::swapBuffers() {
    // Present is already scheduled by the renderer (presentDrawable) — release the frame drawable.
    [m_drawable release];
    m_drawable = nil;
}

uint32_t OSX_MetalContext::getDefaultFrameBufferId() {
    return 0;
}

void *OSX_MetalContext::nativeDevice() {
    return (void *)s_device;
}

void *OSX_MetalContext::nativeDeviceContext() {
    return (void *)s_queue;
}

void *OSX_MetalContext::nativeBackBufferRTV() {
    acquireDrawableIfNeeded();
    return m_drawable != nil ? (void *)m_drawable.texture : nullptr;
}

void *OSX_MetalContext::nativeDepthStencilView() {
    acquireDrawableIfNeeded();
    if (m_drawable == nil) { return nullptr; }
    ensureDepthTexture(m_drawable.texture);
    return (void *)m_depthTexture;
}

void *OSX_MetalContext::nativeCurrentDrawable() {
    return (void *)m_drawable;
}
