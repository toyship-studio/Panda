#include "Panda/Packages/PackageEntry.hpp"
#include "Panda/Terrain/TerrainExtensions.hpp"

namespace Panda::Terrain {

const char *const MODULE_ID = PANDA_PACKAGE_ID;
const char *const MODULE_VERSION = PANDA_PACKAGE_VERSION;

} // namespace Panda::Terrain

// Entry point of the engine part of the terrain: pandaPackageEngine_dev_pandaengine_terrain (the
// id from module.json, see panda_package()); it is called by the editor (the built-in table), the
// game and the smoke test.
PANDA_PACKAGE_ENGINE_ENTRY(Panda::Terrain::registerExtensions)
