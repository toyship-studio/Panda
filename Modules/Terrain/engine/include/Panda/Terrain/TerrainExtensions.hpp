#pragma once

#include "Panda/Assets/Base/Asset.hpp"

namespace Panda {

class Extensions;

namespace Terrain {

    // Module passport from module.json (via the build) — for PackageLoader::loadStatic of the hosts
    // that link the terrain statically.
    extern const char *const MODULE_ID;
    extern const char *const MODULE_VERSION;

    // Terrain asset kinds: the ids are persisted in .pmeta and the runtime registry — do not change.
    inline const AssetType TYPE_2D{9};
    inline const AssetType TYPE_3D{10};

    // Contributions of the terrain to the engine registries: the Terrain2D/3D component types, the
    // .terrain asset kinds (one format for the editor and the runtime) and a world system (the
    // terrains' frame, crowns for the occlusion volume, static physics bodies). The terrain is an
    // in-tree plugin (context/plugins.md, stage 0): called by the application at startup, before
    // the first world.
    void registerExtensions(Extensions &extensions);

} // namespace Terrain

} // namespace Panda
