#pragma once

#include <Panda/Particles/ParticlesScriptApi.hpp>

namespace Bamboo::Internal {

// The "particles" contribution table on the host — by name and version; nullptr — there is no
// Particles module in the engine (the calls do nothing and return zeros). Lives while the
// contribution is loaded; the script module is reloaded as a whole, the cache lasts for its
// lifetime.
const Panda::Scripting::ParticlesHostApi *particlesTable();

} // namespace Bamboo::Internal
