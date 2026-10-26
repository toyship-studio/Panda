#include "Bamboo/UI/FontAPI.hpp"

#include "Bamboo/Assets/FontAPI.hpp"
#include "Bamboo/Internal/MainThread.hpp"
#include "Bamboo/UI/UIRuntime.hpp"
#include "Bamboo/UI/WindowRegistry.hpp"

#include <string>

namespace Bamboo::UI {

PandaUI::FontFamily registerFont(FontHandle font) {
    if (!Bamboo::Internal::requireMainThreadApi("UI::registerFont")) { return PandaUI::FontFamily::Sans; }
    if (font.id == BAMBOO_INVALID_HANDLE) { return PandaUI::FontFamily::Sans; }
    const PandaUI::FontFamily family = PandaUI::fontFamily("asset:" + std::to_string(font.id));
    WindowRegistry &registry = UIRuntime::Internal::registry();
    if (registry.hasFont(family)) { return family; }
    std::vector<unsigned char> data = FontAPI::readData(font);
    if (data.empty() || !registry.registerFont(family, std::move(data))) { return PandaUI::FontFamily::Sans; }
    return family;
}

PandaUI::Font font(FontHandle handle, float size, PandaUI::FontWeight weight) {
    return PandaUI::Font(size, weight, registerFont(handle));
}

} // namespace Bamboo::UI
