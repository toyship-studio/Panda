#include "Panda/Audio/AudioExtensions.hpp"
#include "Panda/Packages/PackageEntry.hpp"

namespace Panda::Audio {

const char *const MODULE_ID = PANDA_PACKAGE_ID;
const char *const MODULE_VERSION = PANDA_PACKAGE_VERSION;

} // namespace Panda::Audio

// Entry point of the engine part: pandaPackageEngine_dev_pandaengine_audio (see panda_package()).
PANDA_PACKAGE_ENGINE_ENTRY(Panda::Audio::registerExtensions)
