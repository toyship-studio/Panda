#pragma once

#include "ScriptManifest.hpp"
#include <PandaUI/Rendering/DrawList.hpp>
#include <PandaUI/Core/Event.hpp>

namespace Panda::Scripting {

constexpr uint32_t SCRIPT_ABI_VERSION = 29;

using ClearFn = void (*)();
using InstantiateScriptFn = Handle (*)(Handle entityHandle, const char *name);
using SetFieldValueFn = void (*)(Handle entityHandle, Handle scriptID, FieldHandle fieldId, void *value);
using InvokeUpdateAtScriptFn = void (*)(Handle entityHandle, Handle scriptID, float deltaTime);
// v27: before every physics step, with the step length.
using InvokeFixedUpdateAtScriptFn = void (*)(Handle entityHandle, Handle scriptID, float stepTime);
using InvokeLateUpdateAtScriptFn = void (*)(Handle entityHandle, Handle scriptID, float deltaTime);
using InvokeStartAtScriptFn = void (*)(Handle entityHandle, Handle scriptID);
using InvokeBeginCollisionTouchFn = void (*)(Handle entityHandle, Handle scriptID, Handle collisionEntityHandle);
using InvokeEndCollisionTouchFn = void (*)(Handle entityHandle, Handle scriptID, Handle collisionEntityHandle);
using InvokeBeginSensorOverlapFn = void (*)(Handle entityHandle, Handle scriptID, Handle sensorEntityHandle);
using InvokeEndSensorOverlapFn = void (*)(Handle entityHandle, Handle scriptID, Handle sensorEntityHandle);
using EntityWillBeDeletedFn = void (*)(Handle entityHandle);
// v28: flat arrays owned by the bundle (ScriptManifest.hpp).
// v29: the physics functions live in the table of the physics module (hostApiTable_Find).
using GetManifestFn = ScriptBundleInfo (*)();
using UIUpdateFn = void (*)(float deltaTime);
using UIHandleEventFn = bool (*)(const PandaUI::Event *event);
using UIWindowClosedFn = void (*)(Handle windowHandle);

// v23: script background tasks (Bamboo::Async on top of PandaAsync inside the script module).
// The scheduler lives on the module side; the engine only drives it: it applies finished
// completions on the main thread and must stop the workers before the script instances are
// destroyed and before the dynamic library is unloaded.
// async_WaitAll does not execute the completion queue — it waits for the workers and
// discards unsafe deferred calls.
using AsyncUpdateFn = void (*)(uint32_t maxCompletions, double maxMilliseconds);
using AsyncCancelAllFn = void (*)();
using AsyncWaitAllFn = void (*)();

using ApplicationQuitFn = void (*)();
using ApplicationGetWidthFn = uint32_t (*)();
using ApplicationGetHeightFn = uint32_t (*)();
using ApplicationGetTimeFn = float (*)();
using ApplicationIsCursorLockedFn = bool (*)();
using ApplicationSetCursorLockedFn = void (*)(bool locked);
// Game save directory (editor: <project>/.Panda/SaveData; runtime: the OS path for the game).
// The "return the size / fill the buffer" pattern (like ui_LoadStaticResource): returns the path
// length without the null byte; destination null — a length query only.
using ApplicationGetPersistentDataPathFn = uint32_t (*)(void *destination, uint32_t destinationSize);

using WorldLoadFn = void (*)(Handle worldHandle);
using WorldFindByTagFn = Handle (*)(const char *tag);
using WorldFindAllByTagFn = uint32_t (*)(const char *tag, Handle *entityHandles, uint32_t capacity);
using WorldCreateEntityFn = Handle (*)(const char *tag);
using WorldDestroyEntityFn = void (*)(Handle id);

using WorldLightingSetAmbientColorFn = void (*)(float r, float g, float b, float a);
using WorldLightingSetAmbientIntensityFn = void (*)(float intensity);

using InputIsKeyPressedFn = bool (*)(int key);
using InputIsKeyJustPressedFn = bool (*)(int key);
using InputIsMouseButtonPressedFn = bool (*)(int key);
using InputIsMouseButtonJustPressedFn = bool (*)(int key);
using InputGetMouseDeltaXFn = double (*)();
using InputGetMouseDeltaYFn = double (*)();
using InputGetTouchCountFn = int (*)();
using InputGetTouchFn = bool (*)(int index, int *id, float *x, float *y);

using EntityCreateComponentFn = void (*)(Handle entityHandle, const char *type);
using EntityHasComponentFn = bool (*)(Handle entityHandle, const char *type);
using EntityRemoveComponentFn = void (*)(Handle entityHandle, const char *type);
using EntityGetNameFn = const char *(*)(Handle entityHandle);

using TransformComponentGetPositionFn = void (*)(Handle entityHandle, float *x, float *y, float *z);
using TransformComponentSetPositionFn = void (*)(Handle entityHandle, float x, float y, float z);
using TransformComponentTranslateFn = void (*)(Handle entityHandle, float x, float y, float z);
using TransformComponentGetRotationEulerFn = void (*)(Handle entityHandle, float *x, float *y, float *z);
using TransformComponentSetRotationEulerFn = void (*)(Handle entityHandle, float x, float y, float z);
using TransformComponentGetRotationFn = void (*)(Handle entityHandle, float *w, float *x, float *y, float *z);
using TransformComponentSetRotationFn = void (*)(Handle entityHandle, float w, float x, float y, float z);
using TransformComponentRotateEulerFn = void (*)(Handle entityHandle, float x, float y, float z);
using TransformComponentGetScaleFn = void (*)(Handle entityHandle, float *x, float *y, float *z);
using TransformComponentSetScaleFn = void (*)(Handle entityHandle, float x, float y, float z);

using SpriteRendererComponentGetColorFn = void (*)(Handle entityHandle, float *r, float *g, float *b, float *a);
using SpriteRendererComponentSetColorFn = void (*)(Handle entityHandle, float r, float g, float b, float a);
using SpriteRendererComponentGetMaterialFn = void (*)(Handle entityHandle, Handle *materialHandle);
using SpriteRendererComponentSetMaterialFn = void (*)(Handle entityHandle, Handle materialHandle);
using SpriteRendererComponentGetTextureFn = void (*)(Handle entityHandle, Handle *textureHandle);
using SpriteRendererComponentSetTextureFn = void (*)(Handle entityHandle, Handle textureHandle);
using SpriteRendererComponentSetCellFn = void (*)(Handle entityHandle, int cols, int rows, int index);

using MeshComponentGetMeshFn = void (*)(Handle entityHandle, Handle *meshHandle);
using MeshComponentSetMeshFn = void (*)(Handle entityHandle, Handle meshHandle);
using MeshComponentSetMaterialFn = void (*)(Handle entityHandle, Handle materialHandle);

using DirectionalLightComponentSetColorFn = void (*)(Handle entityHandle, float r, float g, float b, float a);
using DirectionalLightComponentSetIntensityFn = void (*)(Handle entityHandle, float intensity);
using DirectionalLightComponentSetEnabledFn = void (*)(Handle entityHandle, bool enabled);

// Camera postprocessing: enabling/disabling the chain from the game.
using CameraComponentSetPostprocessingEnabledFn = void (*)(Handle entityHandle, uint32_t enabled);
using CameraComponentGetPostprocessingEnabledFn = void (*)(Handle entityHandle, uint32_t *enabled);
using CameraComponentSetBloomFn = void (*)(Handle entityHandle, uint32_t enabled, float intensity, float radius);
using CameraComponentGetBloomFn = void (*)(Handle entityHandle, uint32_t *enabled, float *intensity, float *radius);

using MaterialSetFloatValueFn = void (*)(Handle materialHandle, const char *name, float value);
using MaterialSetVec4ValueFn = void (*)(Handle materialHandle, const char *name, float r, float g, float b, float a);
using MaterialSetTextureValueFn = void (*)(Handle materialHandle, const char *name, Handle textureHandle);

using TextureReadPixelsRGBA8Fn =
    uint32_t (*)(Handle textureHandle, uint32_t *width, uint32_t *height, void *destination, uint32_t destinationSize);

// Runtime textures (dynamic: lightmaps, minimaps) — a mirror of runtime meshes.
using TextureCreateRGBA8Fn = void (*)(uint32_t width, uint32_t height, Handle *handle);
using TextureUpdateRGBA8Fn = void (*)(Handle handle, const void *pixels, uint32_t sizeBytes);
using TextureDeleteFn = void (*)(Handle handle);

using MeshCreateFn = void (*)(Handle *handle);
using MeshUpdateFn =
    void (*)(Handle handle, const void *vertices, uint32_t verticesCount, const void *indices, uint32_t indicesCount);
using MeshDeleteFn = void (*)(Handle meshHandle);

// Named function tables of contributions (plugins): the script module requests a table by
// name and version (exact match) — scripting extensions do not touch the layout of
// ScriptHostApi. A table is a flat C struct of the contribution with a layout version; it lives
// as long as the contribution is loaded. nullptr — no such contribution or a wrong version.
using HostApiTableFindFn = const void *(*)(const char *name, uint32_t version);

using ConsoleLogFn = void (*)(int type, const char *);

using UIGetMainWindowFn = Handle (*)();
using UICreateWindowFn = Handle (*)(const char *title, float x, float y, float width, float height);
using UIDestroyWindowFn = void (*)(Handle windowHandle);
using UIGetWindowViewportFn = bool (*)(
    Handle windowHandle,
    float *width,
    float *height,
    float *dpiX,
    float *dpiY,
    float *safeTop,
    float *safeLeft,
    float *safeRight,
    float *safeBottom
);
using UICreateTextureRGBA8Fn = uint64_t (*)(const uint8_t *pixels, uint16_t width, uint16_t height);
using UIDestroyTextureFn = void (*)(uint64_t textureId);
using UILoadStaticResourceFn = uint32_t (*)(const char *path, void *destination, uint32_t destinationSize);
using UISubmitDrawListFn = void (*)(Handle windowHandle, const PandaUI::DrawListView *drawList);
using UISetCursorFn = void (*)(uint32_t cursor);
// Clipboard: get — the "return the size / fill the buffer" pattern (like UILoadStaticResource).
using UIGetClipboardTextFn = uint32_t (*)(void *destination, uint32_t destinationSize);
using UISetClipboardTextFn = void (*)(const char *text);

struct ScriptPluginApi final {
    uint32_t abiVersion = SCRIPT_ABI_VERSION;
    ClearFn clear = nullptr;
    InstantiateScriptFn instantiateScript = nullptr;
    SetFieldValueFn setFieldValue = nullptr;
    InvokeUpdateAtScriptFn invokeUpdateAtScript = nullptr;
    InvokeLateUpdateAtScriptFn invokeLateUpdateAtScript = nullptr;
    InvokeStartAtScriptFn invokeStartAtScript = nullptr;
    InvokeBeginCollisionTouchFn invokeBeginCollisionTouch = nullptr;
    InvokeEndCollisionTouchFn invokeEndCollisionTouch = nullptr;
    InvokeBeginSensorOverlapFn invokeBeginSensorOverlap = nullptr;
    InvokeEndSensorOverlapFn invokeEndSensorOverlap = nullptr;
    EntityWillBeDeletedFn entityWillBeDeleted = nullptr;
    GetManifestFn getManifest = nullptr;
    UIUpdateFn ui_Update = nullptr;
    UIHandleEventFn ui_HandleEvent = nullptr;
    UIWindowClosedFn ui_WindowClosed = nullptr;
    // v23: script background tasks (only at the end — ABI compatibility of the layout).
    AsyncUpdateFn async_Update = nullptr;
    AsyncCancelAllFn async_CancelAll = nullptr;
    AsyncWaitAllFn async_WaitAll = nullptr;
    // v27: the fixed physics step.
    InvokeFixedUpdateAtScriptFn invokeFixedUpdateAtScript = nullptr;
};

struct ScriptHostApi final {
    uint32_t abiVersion = SCRIPT_ABI_VERSION;
    ApplicationQuitFn application_Quit = nullptr;
    ApplicationGetWidthFn application_GetWidth = nullptr;
    ApplicationGetHeightFn application_GetHeight = nullptr;
    ApplicationGetTimeFn application_GetTime = nullptr;
    ApplicationIsCursorLockedFn application_IsCursorLocked = nullptr;
    ApplicationSetCursorLockedFn application_SetCursorLocked = nullptr;
    ApplicationGetPersistentDataPathFn application_GetPersistentDataPath = nullptr;
    WorldLoadFn world_Load = nullptr;
    WorldFindByTagFn world_FindByTag = nullptr;
    WorldFindAllByTagFn world_FindAllByTag = nullptr;
    WorldCreateEntityFn world_CreateEntity = nullptr;
    WorldDestroyEntityFn world_DestroyEntity = nullptr;
    WorldLightingSetAmbientColorFn worldLighting_SetAmbientColor = nullptr;
    WorldLightingSetAmbientIntensityFn worldLighting_SetAmbientIntensity = nullptr;
    InputIsKeyPressedFn input_IsKeyPressed = nullptr;
    InputIsKeyJustPressedFn input_IsKeyJustPressed = nullptr;
    InputIsMouseButtonPressedFn input_IsMouseButtonPressed = nullptr;
    InputIsMouseButtonJustPressedFn input_IsMouseButtonJustPressed = nullptr;
    InputGetMouseDeltaXFn input_GetMouseDeltaX = nullptr;
    InputGetMouseDeltaYFn input_GetMouseDeltaY = nullptr;
    InputGetTouchCountFn input_GetTouchCount = nullptr;
    InputGetTouchFn input_GetTouch = nullptr;
    EntityCreateComponentFn entity_CreateComponent = nullptr;
    EntityHasComponentFn entity_HasComponent = nullptr;
    EntityRemoveComponentFn entity_RemoveComponent = nullptr;
    EntityGetNameFn entity_GetName = nullptr;
    TransformComponentGetPositionFn transformComponent_GetPosition = nullptr;
    TransformComponentSetPositionFn transformComponent_SetPosition = nullptr;
    TransformComponentTranslateFn transformComponent_Translate = nullptr;
    TransformComponentGetRotationEulerFn transformComponent_GetRotationEuler = nullptr;
    TransformComponentSetRotationEulerFn transformComponent_SetRotationEuler = nullptr;
    TransformComponentGetRotationFn transformComponent_GetRotation = nullptr;
    TransformComponentSetRotationFn transformComponent_SetRotation = nullptr;
    TransformComponentRotateEulerFn transformComponent_RotateEuler = nullptr;
    TransformComponentGetScaleFn transformComponent_GetScale = nullptr;
    TransformComponentSetScaleFn transformComponent_SetScale = nullptr;
    SpriteRendererComponentGetColorFn spriteRendererComponent_GetColor = nullptr;
    SpriteRendererComponentSetColorFn spriteRendererComponent_SetColor = nullptr;
    SpriteRendererComponentGetMaterialFn spriteRendererComponent_GetMaterial = nullptr;
    SpriteRendererComponentSetMaterialFn spriteRendererComponent_SetMaterial = nullptr;
    SpriteRendererComponentGetTextureFn spriteRendererComponent_GetTexture = nullptr;
    SpriteRendererComponentSetTextureFn spriteRendererComponent_SetTexture = nullptr;
    SpriteRendererComponentSetCellFn spriteRendererComponent_SetCell = nullptr;
    MeshComponentGetMeshFn meshComponent_GetMesh = nullptr;
    MeshComponentSetMeshFn meshComponent_SetMesh = nullptr;
    MeshComponentSetMaterialFn meshComponent_SetMaterial = nullptr;
    DirectionalLightComponentSetColorFn directionalLightComponent_SetColor = nullptr;
    DirectionalLightComponentSetIntensityFn directionalLightComponent_SetIntensity = nullptr;
    DirectionalLightComponentSetEnabledFn directionalLightComponent_SetEnabled = nullptr;
    CameraComponentSetPostprocessingEnabledFn cameraComponent_SetPostprocessingEnabled = nullptr;
    CameraComponentGetPostprocessingEnabledFn cameraComponent_GetPostprocessingEnabled = nullptr;
    CameraComponentSetBloomFn cameraComponent_SetBloom = nullptr;
    CameraComponentGetBloomFn cameraComponent_GetBloom = nullptr;
    MaterialSetFloatValueFn material_SetFloatValue = nullptr;
    MaterialSetVec4ValueFn material_SetVec4Value = nullptr;
    MaterialSetTextureValueFn material_SetTextureValue = nullptr;
    TextureReadPixelsRGBA8Fn texture_ReadPixelsRGBA8 = nullptr;
    MeshCreateFn mesh_Create = nullptr;
    MeshUpdateFn mesh_Update = nullptr;
    MeshDeleteFn mesh_Delete = nullptr;
    ConsoleLogFn console_Log = nullptr;
    UIGetMainWindowFn ui_GetMainWindow = nullptr;
    UICreateWindowFn ui_CreateWindow = nullptr;
    UIDestroyWindowFn ui_DestroyWindow = nullptr;
    UIGetWindowViewportFn ui_GetWindowViewport = nullptr;
    UICreateTextureRGBA8Fn ui_CreateTextureRGBA8 = nullptr;
    UIDestroyTextureFn ui_DestroyTexture = nullptr;
    UILoadStaticResourceFn ui_LoadStaticResource = nullptr;
    UISubmitDrawListFn ui_SubmitDrawList = nullptr;
    UISetCursorFn ui_SetCursor = nullptr;
    UIGetClipboardTextFn ui_GetClipboardText = nullptr;
    UISetClipboardTextFn ui_SetClipboardText = nullptr;
    // v22: runtime textures (only at the end — ABI compatibility of the layout).
    TextureCreateRGBA8Fn texture_CreateRGBA8 = nullptr;
    TextureUpdateRGBA8Fn texture_UpdateRGBA8 = nullptr;
    TextureDeleteFn texture_Delete = nullptr;
    // v24: named contribution tables (contribution functions moved from this table into their own).
    HostApiTableFindFn hostApiTable_Find = nullptr;
};

using LoadScriptPluginFn = const ScriptPluginApi *(*)(const ScriptHostApi *hostApi);

} // namespace Panda::Scripting

namespace Panda {

using ScriptPluginApi = Scripting::ScriptPluginApi;
using ScriptHostApi = Scripting::ScriptHostApi;
using LoadScriptPluginFn = Scripting::LoadScriptPluginFn;

} // namespace Panda
