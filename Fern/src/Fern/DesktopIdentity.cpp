#include "Fern/Fern.hpp"

#include <string>

namespace Fern {

namespace {
    std::string s_windowClass;
    std::string s_appId;
    bool s_startsInBackground = false;
} // namespace

void setStartsInBackground(bool background) {
    s_startsInBackground = background;
}

bool startsInBackground() {
    return s_startsInBackground;
}

void setDesktopIdentity(const char *windowClass, const char *appId) {
    s_windowClass = windowClass ? windowClass : "";
    s_appId = appId ? appId : "";
}

const char *getDesktopWindowClass() {
    return s_windowClass.c_str();
}

const char *getDesktopAppId() {
    return s_appId.c_str();
}

} // namespace Fern
