#pragma once

#include <Panda/Audio/AudioScriptApi.hpp>

namespace Bamboo::Internal {

// The "audio" contribution table on the host — by name and version; nullptr — there is no Audio
// module in the engine (the calls do nothing and return zeros). Lives while the contribution is
// loaded; the script module is reloaded as a whole, the cache lasts for its lifetime.
const Panda::Scripting::AudioHostApi *audioTable();

} // namespace Bamboo::Internal
