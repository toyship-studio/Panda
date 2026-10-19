#pragma once

#include "Panda/Extensions/ScriptHostApis.hpp"

namespace Panda {

namespace Terrain {

    // The "terrain" table for scripts (the ABI is Panda/Terrain/TerrainScriptApi.hpp).
    ScriptHostApiTable scriptHostApiTable();

} // namespace Terrain

} // namespace Panda
