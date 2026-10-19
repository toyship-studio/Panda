#pragma once

#include "Panda/ScriptEngine/ABI/ScriptApi.hpp"

#include <cstdint>

namespace Panda {

class Entity;

// Entity by a handle from a script: a broken reference → invalid Entity + a one-time error in
// the console (the call does a no-op). For core functions and contribution tables (ScriptHostApis).
Entity resolveScriptEntity(Handle entityHandle);

namespace ExternalCalls {
    using Clear = Scripting::ClearFn;
    extern Clear clear;

    using InstantiateScript = Scripting::InstantiateScriptFn;
    extern InstantiateScript instantiateScript;

    using SetFieldValue = Scripting::SetFieldValueFn;
    extern SetFieldValue setFieldValue;

    using InvokeUpdateAtScript = Scripting::InvokeUpdateAtScriptFn;
    extern InvokeUpdateAtScript invokeUpdateAtScript;

    using InvokeFixedUpdateAtScript = Scripting::InvokeFixedUpdateAtScriptFn;
    extern InvokeFixedUpdateAtScript invokeFixedUpdateAtScript;

    using InvokeLateUpdateAtScript = Scripting::InvokeLateUpdateAtScriptFn;
    extern InvokeLateUpdateAtScript invokeLateUpdateAtScript;

    using InvokeStartAtScript = Scripting::InvokeStartAtScriptFn;
    extern InvokeStartAtScript invokeStartAtScript;

    using InvokeBeginCollisionTouch = Scripting::InvokeBeginCollisionTouchFn;
    extern InvokeBeginCollisionTouch invokeBeginCollisionTouch;

    using InvokeEndCollisionTouch = Scripting::InvokeEndCollisionTouchFn;
    extern InvokeEndCollisionTouch invokeEndCollisionTouch;

    using InvokeBeginSensorOverlap = Scripting::InvokeBeginSensorOverlapFn;
    extern InvokeBeginSensorOverlap invokeBeginSensorOverlap;

    using InvokeEndSensorOverlap = Scripting::InvokeEndSensorOverlapFn;
    extern InvokeEndSensorOverlap invokeEndSensorOverlap;

    using EntityWillBeDeleted = Scripting::EntityWillBeDeletedFn;
    extern EntityWillBeDeleted entityWillBeDeleted;

    using GetManifest = Scripting::GetManifestFn;
    extern GetManifest getManifest;

    using UIUpdate = Scripting::UIUpdateFn;
    extern UIUpdate ui_Update;
    using UIHandleEvent = Scripting::UIHandleEventFn;
    extern UIHandleEvent ui_HandleEvent;
    using UIWindowClosed = Scripting::UIWindowClosedFn;
    extern UIWindowClosed ui_WindowClosed;

    using AsyncUpdate = Scripting::AsyncUpdateFn;
    extern AsyncUpdate async_Update;
    using AsyncCancelAll = Scripting::AsyncCancelAllFn;
    extern AsyncCancelAll async_CancelAll;
    using AsyncWaitAll = Scripting::AsyncWaitAllFn;
    extern AsyncWaitAll async_WaitAll;
} // namespace ExternalCalls

// Applies finished script background tasks on the main thread. Called once per frame
// while the scripts are being updated.
void updateScriptAsync(uint32_t maxCompletions, double maxMilliseconds);

// Cancels the script background tasks and waits for the workers. Mandatory before the script
// instances are destroyed (play mode stop) and before the module is unloaded: otherwise a
// worker or a deferred completion would execute code that no longer exists.
void cancelAndWaitScriptAsync();

const ScriptHostApi *getScriptHostApi();
void bindScriptPluginApi(const ScriptPluginApi *api);
void clearScriptPluginApi();
void freeScriptingResources();

} // namespace Panda
