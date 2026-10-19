#pragma once

#include <Foundation/PlatformDetection.hpp>

namespace Foundation {

// Graphics APIs of the engine. The single enumeration for all layers:
// Fern (window context), Miren (renderer implementation), assets (shader folders).
enum class GraphicsAPI {
    OpenGL,
    OpenGLES,
    D3D11,
    Metal,  // reserved for the future
    Vulkan, // reserved for the future
};

// API conventions — all related parameters in one place.
struct GraphicsAPITraits {
    // Shader folder name (.Panda/Shaders/cooked/<name>, generated_resources/<name>).
    const char *shaderBackendName;
    // Render-to-texture: row v=0 is the bottom of the image (GL) or the top (D3D/Metal/Vulkan).
    bool originBottomLeft;
    // NDC depth range: [-1..1] (GL) or [0..1] (D3D/Metal/Vulkan).
    bool homogeneousDepth;
};

constexpr GraphicsAPITraits getGraphicsAPITraits(GraphicsAPI api) {
    switch (api) {
        case GraphicsAPI::OpenGLES:
            return {"opengles", true, true};
        case GraphicsAPI::D3D11:
            return {"d3d11", false, false};
        case GraphicsAPI::Metal:
            return {"metal", false, false};
        case GraphicsAPI::Vulkan:
            return {"vulkan", false, false};
        case GraphicsAPI::OpenGL:
            break;
    }
    return {"opengl", true, true};
}

// Active API of the build. There is a single selection point — the PANDA_SHADER_BACKEND CMake
// option: shader resources are baked for it, Fern creates contexts of this type,
// Miren selects the renderer implementation.
#if defined(PANDA_GRAPHICS_D3D11)
constexpr GraphicsAPI ACTIVE_GRAPHICS_API = GraphicsAPI::D3D11;
#elif defined(PANDA_GRAPHICS_METAL)
constexpr GraphicsAPI ACTIVE_GRAPHICS_API = GraphicsAPI::Metal;
#elif defined(PLATFORM_IOS) || defined(PLATFORM_ANDROID)
constexpr GraphicsAPI ACTIVE_GRAPHICS_API = GraphicsAPI::OpenGLES;
#else
constexpr GraphicsAPI ACTIVE_GRAPHICS_API = GraphicsAPI::OpenGL;
#endif

constexpr GraphicsAPITraits ACTIVE_GRAPHICS_TRAITS = getGraphicsAPITraits(ACTIVE_GRAPHICS_API);

} // namespace Foundation
