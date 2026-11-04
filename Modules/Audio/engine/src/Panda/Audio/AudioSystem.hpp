#pragma once

#include <Panda/Extensions/ScriptHostApis.hpp>
#include <Panda/Extensions/WorldSystems.hpp>

namespace Panda::Audio {

// The "audio" world system (AudioSystem.cpp): the voices of the AudioSourceComponents of a
// running world and the listener; after the scripts in every phase, so that it hears the camera
// the scripts moved in lateUpdate.
WorldSystem worldSystem();

// The "audio" contribution table for scripts (AudioScriptHost.cpp).
ScriptHostApiTable scriptHostApiTable();

} // namespace Panda::Audio
