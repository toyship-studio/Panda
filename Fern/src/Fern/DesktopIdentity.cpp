#include "Fern/Fern.hpp"

#include <string>

namespace Fern {

namespace {
    std::string s_windowClass;
    std::string s_appId;
} // namespace

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
