#pragma once
// The addresses of the portal (panda-portal, context/infrastructure.md): the API the updater and the
// plugin store talk to, the site the editor opens in the browser.
namespace Panda::Portal {

inline constexpr const char *kSite = "https://pandaengine.dev";
inline constexpr const char *kApi = "https://pandaengine.dev/api/v1";
inline constexpr const char *kDownloads = "https://pandaengine.dev/dl";
inline constexpr const char *kDownloadPage = "https://pandaengine.dev/download";
inline constexpr const char *kPluginsPage = "https://pandaengine.dev/plugins";

} // namespace Panda::Portal
