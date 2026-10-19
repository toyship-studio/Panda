#pragma once

#include <Panda/Assets/Base/Asset.hpp> // Panda::AssetType
#include <Panda/GameLogic/Entity.hpp>

namespace Editor {

// Object type icon (a name for icons().get) — editor tabs and Hierarchy rows.
// Entity: by components in the "most characteristic first" order; nothing special — a box.
const char *entityIconName(Panda::Entity entity);
const char *assetIconName(Panda::AssetType type);

} // namespace Editor
