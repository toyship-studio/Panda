#pragma once

#include <cstdint>

namespace Panda {

// Engine version (context/plugins.md): referenced by plugin manifests (`engine`) and the SDK
// package. Changed by hand on release; the root CMakeLists.txt reads it from here for the bundles
// and the release manifest. A plugin built for a different version is not loaded by the editor —
// C++ plugins are tied to the engine build.
inline constexpr const char *ENGINE_VERSION = "0.1.0";

// The build of a release workflow run (context/infrastructure.md): the number grows with every run,
// so an older commit rebuilt after a rollback is a newer build to the updater, and the commit names
// the exact code in the About window and in the manifest. Both come from the build
// (-DPANDA_BUILD_NUMBER, -DPANDA_BUILD_COMMIT: PUBLIC definitions of the Panda target); a local build
// and the SDK package, which carries no definitions, are build 0 without a commit.
#ifdef PANDA_BUILD_NUMBER
inline constexpr uint32_t ENGINE_BUILD_NUMBER = PANDA_BUILD_NUMBER;
#else
inline constexpr uint32_t ENGINE_BUILD_NUMBER = 0;
#endif
#ifdef PANDA_BUILD_COMMIT
inline constexpr const char *ENGINE_BUILD_COMMIT = PANDA_BUILD_COMMIT;
#else
inline constexpr const char *ENGINE_BUILD_COMMIT = "";
#endif

// The release channel of this build ("preview", later "stable"): the channel the updater follows
// (-DPANDA_RELEASE_CHANNEL of the release workflow). Empty for a local build: no updates.
#ifdef PANDA_RELEASE_CHANNEL
inline constexpr const char *ENGINE_RELEASE_CHANNEL = PANDA_RELEASE_CHANNEL;
#else
inline constexpr const char *ENGINE_RELEASE_CHANNEL = "";
#endif

} // namespace Panda
