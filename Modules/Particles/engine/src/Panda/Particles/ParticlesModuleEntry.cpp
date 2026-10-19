#include "Panda/Packages/PackageEntry.hpp"
#include "Panda/Particles/ParticlesExtensions.hpp"

namespace Panda::Particles {

const char *const MODULE_ID = PANDA_PACKAGE_ID;
const char *const MODULE_VERSION = PANDA_PACKAGE_VERSION;

} // namespace Panda::Particles

// Entry point of the engine part: pandaPackageEngine_dev_pandaengine_particles (see panda_package()).
PANDA_PACKAGE_ENGINE_ENTRY(Panda::Particles::registerExtensions)
