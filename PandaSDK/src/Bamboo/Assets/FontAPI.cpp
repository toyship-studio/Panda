#include "Bamboo/Assets/FontAPI.hpp"

#include "Bamboo/Internal/MainThread.hpp"
#include "Panda/OuterScriptHook.hpp"

namespace Bamboo::FontAPI {

std::vector<unsigned char> readData(FontHandle font) {
    if (!Internal::requireMainThreadApi("FontAPI::readData")) { return {}; }
    if (font.id == BAMBOO_INVALID_HANDLE || !Panda::ExternalCalls::font_ReadData) { return {}; }
    const uint32_t size = Panda::ExternalCalls::font_ReadData(font.id, nullptr, 0);
    if (size == 0) { return {}; }
    std::vector<unsigned char> data(size);
    if (Panda::ExternalCalls::font_ReadData(font.id, data.data(), size) != size) { return {}; }
    return data;
}

} // namespace Bamboo::FontAPI
