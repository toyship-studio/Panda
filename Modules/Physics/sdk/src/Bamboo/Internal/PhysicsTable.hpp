#pragma once

#include <Panda/Physics/PhysicsScriptApi.hpp>

namespace Bamboo::Internal {

// The "physics" contribution table on the host — by name and version; nullptr — there is no
// physics module in the engine (the calls do nothing and return zeros). Lives while the
// contribution is loaded; the script module is reloaded as a whole, the cache lasts for its
// lifetime.
const Panda::Scripting::PhysicsHostApi *physicsTable();

} // namespace Bamboo::Internal
