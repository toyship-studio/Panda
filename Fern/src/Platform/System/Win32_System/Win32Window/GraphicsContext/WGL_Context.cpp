#include "WGL_Context.hpp"

#include <Foundation/Logger.hpp>

#include <glad/glad.h>

namespace Fern {

namespace {

    // glad needs a loader that first asks WGL (extension functions) and then
    // falls back to opengl32.dll exports (GL 1.1 core entry points).
    void *wglLoaderFunction(const char *name) {
        void *pointer = reinterpret_cast<void *>(wglGetProcAddress(name));
        if (pointer && pointer != reinterpret_cast<void *>(1) && pointer != reinterpret_cast<void *>(2) &&
            pointer != reinterpret_cast<void *>(3) && pointer != reinterpret_cast<void *>(-1)) {
            return pointer;
        }
        static HMODULE s_opengl32 = LoadLibraryExW(L"opengl32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!s_opengl32) { return nullptr; }
        return reinterpret_cast<void *>(GetProcAddress(s_opengl32, name));
    }

    using SwapIntervalFn = BOOL(WINAPI *)(int);
    using CreateContextAttribsFn = HGLRC(WINAPI *)(HDC, HGLRC, const int *);

    // WGL_ARB_create_context constants.
    constexpr int WGL_CONTEXT_MAJOR_VERSION_ARB = 0x2091;
    constexpr int WGL_CONTEXT_MINOR_VERSION_ARB = 0x2092;
    constexpr int WGL_CONTEXT_PROFILE_MASK_ARB = 0x9126;
    constexpr int WGL_CONTEXT_CORE_PROFILE_BIT_ARB = 0x00000001;

} // namespace

WGL_Context::WGL_Context(HWND window, WGL_Context *sharedContext)
    : m_window(window)
    , m_deviceContext(nullptr)
    , m_context(nullptr) {
    m_deviceContext = GetDC(m_window);
    if (!m_deviceContext) {
        LOG_CRITICAL("WGL: Failed to get device context");
        return;
    }

    PIXELFORMATDESCRIPTOR descriptor = {};
    descriptor.nSize = sizeof(descriptor);
    descriptor.nVersion = 1;
    descriptor.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    descriptor.iPixelType = PFD_TYPE_RGBA;
    descriptor.cColorBits = 32;
    descriptor.cDepthBits = 24;
    descriptor.cStencilBits = 8;
    descriptor.iLayerType = PFD_MAIN_PLANE;

    const int pixelFormat = ChoosePixelFormat(m_deviceContext, &descriptor);
    if (pixelFormat == 0 || !SetPixelFormat(m_deviceContext, pixelFormat, &descriptor)) {
        LOG_CRITICAL("WGL: Failed to set pixel format");
        return;
    }

    // The engine targets OpenGL 3.3 core (same as the macOS backend). A dummy
    // legacy context is needed first to resolve wglCreateContextAttribsARB.
    HGLRC legacyContext = wglCreateContext(m_deviceContext);
    if (!legacyContext) {
        LOG_CRITICAL("WGL: Failed to create OpenGL context");
        return;
    }
    wglMakeCurrent(m_deviceContext, legacyContext);
    auto createContextAttribs =
        reinterpret_cast<CreateContextAttribsFn>(wglGetProcAddress("wglCreateContextAttribsARB"));
    wglMakeCurrent(nullptr, nullptr);

    HGLRC sharedHandle = sharedContext ? sharedContext->m_context : nullptr;
    if (createContextAttribs) {
        const int attribs[] = {
            WGL_CONTEXT_MAJOR_VERSION_ARB,
            3,
            WGL_CONTEXT_MINOR_VERSION_ARB,
            3,
            WGL_CONTEXT_PROFILE_MASK_ARB,
            WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
            0,
        };
        m_context = createContextAttribs(m_deviceContext, sharedHandle, attribs);
    }
    if (m_context) {
        wglDeleteContext(legacyContext);
    } else {
        // Fallback: legacy context + explicit sharing.
        LOG_ERROR("WGL: Core profile context unavailable, falling back to legacy");
        m_context = legacyContext;
        if (sharedHandle && !wglShareLists(sharedHandle, m_context)) {
            LOG_ERROR("WGL: Failed to share OpenGL contexts");
        }
    }

    wglMakeCurrent(m_deviceContext, m_context);
    static bool s_gladLoaded = false;
    if (!s_gladLoaded) {
        s_gladLoaded = true;
        if (gladLoadGLLoader(reinterpret_cast<GLADloadproc>(wglLoaderFunction)) == 0) {
            LOG_CRITICAL("Failed to initialize GLAD");
        }
    }
    // Enable v-sync
    auto swapInterval = reinterpret_cast<SwapIntervalFn>(wglGetProcAddress("wglSwapIntervalEXT"));
    if (swapInterval) { swapInterval(1); }
    wglMakeCurrent(nullptr, nullptr);
}

WGL_Context::~WGL_Context() {
    if (wglGetCurrentContext() == m_context) { wglMakeCurrent(nullptr, nullptr); }
    if (m_context) { wglDeleteContext(m_context); }
    if (m_deviceContext) { ReleaseDC(m_window, m_deviceContext); }
}

void WGL_Context::setCurrent() {
    wglMakeCurrent(m_deviceContext, m_context);
}

void WGL_Context::releaseDrawingContext() {
    wglMakeCurrent(nullptr, nullptr);
}

void WGL_Context::swapBuffers() {
    SwapBuffers(m_deviceContext);
}

void WGL_Context::update() {}

uint32_t WGL_Context::getDefaultFrameBufferId() {
    return 0;
}

} // namespace Fern
