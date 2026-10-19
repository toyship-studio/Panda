#pragma once

#include "Panda/Extensions/AssetKinds.hpp"

namespace Panda {

// Built-in asset kinds of the engine: names, ids, folders, extensions. Loading and cooking — in
// the editor and runtime handlers. Called by the Extensions constructor.
void registerCoreAssetKinds(AssetKindRegistry &registry);

} // namespace Panda
