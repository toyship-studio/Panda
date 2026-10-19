#include "Panda/Packages/PackageEntry.hpp"
#include "Panda/Physics/PhysicsExtensions.hpp"

namespace Panda::Physics {

const char *const MODULE_ID = PANDA_PACKAGE_ID;
const char *const MODULE_VERSION = PANDA_PACKAGE_VERSION;

} // namespace Panda::Physics

// Entry point of the engine part: pandaPackageEngine_dev_pandaengine_physics (see panda_package()).
PANDA_PACKAGE_ENGINE_ENTRY(Panda::Physics::registerExtensions)
