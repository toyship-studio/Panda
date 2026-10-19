#pragma once

#include "Panda/Extensions/ComponentTypes.hpp"

namespace Panda {

// Core contributions to the component type registry: registration order = order of the entity's
// keys in .world; the names are the established keys of the file. Component ↔ DTO conversions
// (asset references by id, scripts with fields) live here, next to the types. Called by the
// Extensions constructor.
void registerCoreComponentTypes(ComponentTypeRegistry &registry);

} // namespace Panda
