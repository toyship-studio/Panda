#include "Bamboo/Assets/TextureAPI.hpp"

#include "Bamboo/Internal/MainThread.hpp"
#include "Panda/OuterScriptHook.hpp"

namespace Bamboo::TextureAPI {

TexturePixelsRGBA8 readPixelsRGBA8(TextureHandle handle) {
    if (!Internal::requireMainThreadApi("TextureAPI::readPixelsRGBA8")) { return {}; }
    if (handle.id == BAMBOO_INVALID_HANDLE || !Panda::ExternalCalls::texture_ReadPixelsRGBA8) { return {}; }

    uint32_t width = 0;
    uint32_t height = 0;
    const uint32_t size =
        Panda::ExternalCalls::texture_ReadPixelsRGBA8(handle.id, &width, &height, nullptr, 0);
    if (size == 0 || width == 0 || height == 0) { return {}; }

    TexturePixelsRGBA8 result;
    result.width = width;
    result.height = height;
    result.pixels.resize(size);

    const uint32_t requiredSize = Panda::ExternalCalls::texture_ReadPixelsRGBA8(
        handle.id, &result.width, &result.height, result.pixels.data(), size
    );
    if (requiredSize != size) { return {}; }
    return result;
}


void updateRGBA8(TextureHandle handle, const void *pixels, uint32_t sizeBytes) {
    if (!Internal::requireMainThreadApi("TextureAPI::updateRGBA8")) { return; }
    Panda::ExternalCalls::texture_UpdateRGBA8(handle.id, pixels, sizeBytes);
}

} // namespace Bamboo::TextureAPI
