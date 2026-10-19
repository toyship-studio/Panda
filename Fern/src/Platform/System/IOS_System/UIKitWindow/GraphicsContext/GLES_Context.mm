//
// Created by Admin on 11.02.2022.
//

#include "GLES_Context.h"

#include <Foundation/Profile.hpp>

#import <QuartzCore/CAEAGLLayer.h>
#import <UIKit/UIKit.h>
#import <OpenGLES/ES3/gl.h>

#include <algorithm>

GLES_Context::GLES_Context(CAEAGLLayer *eaglLayer)
    : m_context(nil)
    , m_layer(eaglLayer)
    , m_colorRenderBuffer(0)
    , m_depthRenderBuffer(0)
    , m_frameBuffer(0)
    , m_width(0)
    , m_height(0)
    , m_pendingWidth(1)
    , m_pendingHeight(1)
    , m_needsDrawableUpdate(false) {
    m_context = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES3];
    [EAGLContext setCurrentContext: m_context];

    m_layer.contentsScale = UIScreen.mainScreen.nativeScale;
    m_layer.drawableProperties = @{kEAGLDrawablePropertyRetainedBacking : @NO,
                                   kEAGLDrawablePropertyColorFormat     : kEAGLColorFormatRGBA8};
    m_layer.opaque = YES;

    glGenRenderbuffers(1, &m_colorRenderBuffer);
    glGenRenderbuffers(1, &m_depthRenderBuffer);
    glGenFramebuffers(1, &m_frameBuffer);

    refreshDrawableSizeFromLayer();
    resizeDrawableIfNeeded(true);
    [EAGLContext setCurrentContext:nil];
}

GLES_Context::~GLES_Context() {
    lockDrawingContext();
    [EAGLContext setCurrentContext:m_context];
    if (m_frameBuffer) { glDeleteFramebuffers(1, &m_frameBuffer); }
    if (m_depthRenderBuffer) { glDeleteRenderbuffers(1, &m_depthRenderBuffer); }
    if (m_colorRenderBuffer) { glDeleteRenderbuffers(1, &m_colorRenderBuffer); }
    [EAGLContext setCurrentContext:nil];
    [m_context release];
    m_context = nil;
    unlockDrawingContext();
}

void GLES_Context::refreshDrawableSizeFromLayer() {
    if (!m_layer) { return; }
    const CGFloat scale = m_layer.contentsScale > 0 ? m_layer.contentsScale : UIScreen.mainScreen.nativeScale;
    const int width = std::max(1, static_cast<int>(m_layer.bounds.size.width * scale));
    const int height = std::max(1, static_cast<int>(m_layer.bounds.size.height * scale));
    m_pendingWidth.store(width, std::memory_order_relaxed);
    m_pendingHeight.store(height, std::memory_order_relaxed);
}

void GLES_Context::resizeDrawableIfNeeded(bool force) {
    if (!m_layer || !m_context) { return; }

    const int width = m_pendingWidth.load(std::memory_order_relaxed);
    const int height = m_pendingHeight.load(std::memory_order_relaxed);
    if (!force && width == m_width && height == m_height) { return; }

    [EAGLContext setCurrentContext:m_context];

    glBindRenderbuffer(GL_RENDERBUFFER, m_colorRenderBuffer);
    if (![m_context renderbufferStorage:GL_RENDERBUFFER fromDrawable:m_layer]) {
        NSLog(@"Failed to create GLES color renderbuffer storage");
        return;
    }

    glBindRenderbuffer(GL_RENDERBUFFER, m_depthRenderBuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);

    glBindFramebuffer(GL_FRAMEBUFFER, m_frameBuffer);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, m_colorRenderBuffer);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depthRenderBuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, m_colorRenderBuffer);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        NSLog(@"Framebuffer is incomplete!");
        return;
    }

    m_width = width;
    m_height = height;
    NSLog(@"GLES drawable size: %d x %d\n", width, height);
}

void GLES_Context::lockDrawingContext() {
    m_mutex.lock();
}

void GLES_Context::unlockDrawingContext() {
    m_mutex.unlock();
}

void GLES_Context::setCurrent() {
    PANDA_PROFILE_SCOPE_N("GLES_Context::setCurrent");
    [EAGLContext setCurrentContext: m_context];
    const bool force = m_needsDrawableUpdate.exchange(false);
    resizeDrawableIfNeeded(force);
}

void GLES_Context::releaseDrawingContext() {
    [EAGLContext setCurrentContext: nil];
}

void GLES_Context::swapBuffers() {
    PANDA_PROFILE_SCOPE_N("GLES_Context::swapBuffers");
    [m_context presentRenderbuffer: GL_RENDERBUFFER];
}

void GLES_Context::update() {
    refreshDrawableSizeFromLayer();
    m_needsDrawableUpdate = true;
}

uint32_t GLES_Context::getDefaultFrameBufferId() {
    return m_frameBuffer;
}
