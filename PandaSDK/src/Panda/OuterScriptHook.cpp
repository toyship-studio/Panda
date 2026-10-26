//
// Created by Michael Andreichev on 02.08.2024.
//

#include "Panda/OuterScriptHook.hpp"
#include "Panda/ScriptClassMaper.hpp"
#include "Bamboo/ScriptRegistry/ScriptRegistry.hpp"
#include "Bamboo/Script.hpp"
#include "Bamboo/Internal/MainThread.hpp"
#include "Bamboo/Async.hpp"
#include "Bamboo/Net.hpp"

#include "Bamboo/UI/UIRuntime.hpp"

#include <PandaUI/Core/Event.hpp>

namespace Panda {

namespace ExternalCalls {
    Application_Quit application_Quit = nullptr;
    Application_GetWidth application_GetWidth = nullptr;
    Application_GetHeight application_GetHeight = nullptr;
    Application_GetTime application_GetTime = nullptr;
    Application_IsCursorLocked application_IsCursorLocked = nullptr;
    Application_SetCursorLocked application_SetCursorLocked = nullptr;
    Application_GetPersistentDataPath application_GetPersistentDataPath = nullptr;
    World_Load world_Load = nullptr;
    World_FindByTag world_FindByTag = nullptr;
    World_FindAllByTag world_FindAllByTag = nullptr;
    World_CreateEntity world_CreateEntity = nullptr;
    World_DestroyEntity world_DestroyEntity = nullptr;
    WorldLighting_SetAmbientColor worldLighting_SetAmbientColor = nullptr;
    WorldLighting_SetAmbientIntensity worldLighting_SetAmbientIntensity = nullptr;
    Input_IsKeyPressed input_IsKeyPressed = nullptr;
    Input_IsKeyJustPressed input_IsKeyJustPressed = nullptr;
    Input_IsMouseButtonPressed input_IsMouseButtonPressed = nullptr;
    Input_IsMouseButtonJustPressed input_IsMouseButtonJustPressed = nullptr;
    Input_GetMouseDeltaX input_GetMouseDeltaX = nullptr;
    Input_GetMouseDeltaY input_GetMouseDeltaY = nullptr;
    Input_GetTouchCount input_GetTouchCount = nullptr;
    Input_GetTouch input_GetTouch = nullptr;
    Entity_CreateComponent entity_CreateComponent = nullptr;
    Entity_HasComponent entity_HasComponent = nullptr;
    Entity_RemoveComponent entity_RemoveComponent = nullptr;
    Entity_GetName entity_GetName = nullptr;
    TransformComponent_GetPosition transformComponent_GetPosition = nullptr;
    TransformComponent_SetPosition transformComponent_SetPosition = nullptr;
    TransformComponent_Translate transformComponent_Translate = nullptr;
    TransformComponent_GetRotationEuler transformComponent_GetRotationEuler = nullptr;
    TransformComponent_SetRotationEuler transformComponent_SetRotationEuler = nullptr;
    TransformComponent_GetRotation transformComponent_GetRotation = nullptr;
    TransformComponent_SetRotation transformComponent_SetRotation = nullptr;
    TransformComponent_RotateEuler transformComponent_RotateEuler = nullptr;
    TransformComponent_GetScale transformComponent_GetScale = nullptr;
    TransformComponent_SetScale transformComponent_SetScale = nullptr;
    SpriteRendererComponent_GetColor spriteRendererComponent_GetColor = nullptr;
    SpriteRendererComponent_SetColor spriteRendererComponent_SetColor = nullptr;
    SpriteRendererComponent_GetMaterial spriteRendererComponent_GetMaterial = nullptr;
    SpriteRendererComponent_SetMaterial spriteRendererComponent_SetMaterial = nullptr;
    SpriteRendererComponent_GetTexture spriteRendererComponent_GetTexture = nullptr;
    SpriteRendererComponent_SetTexture spriteRendererComponent_SetTexture = nullptr;
    SpriteRendererComponent_SetCell spriteRendererComponent_SetCell = nullptr;
    MeshComponent_GetMesh meshComponent_GetMesh = nullptr;
    MeshComponent_SetMesh meshComponent_SetMesh = nullptr;
    MeshComponent_SetMaterial meshComponent_SetMaterial = nullptr;
    DirectionalLightComponent_SetColor directionalLightComponent_SetColor = nullptr;
    DirectionalLightComponent_SetIntensity directionalLightComponent_SetIntensity = nullptr;
    DirectionalLightComponent_SetEnabled directionalLightComponent_SetEnabled = nullptr;
    CameraComponent_SetPostprocessingEnabled cameraComponent_SetPostprocessingEnabled = nullptr;
    CameraComponent_GetPostprocessingEnabled cameraComponent_GetPostprocessingEnabled = nullptr;
    CameraComponent_SetBloom cameraComponent_SetBloom = nullptr;
    CameraComponent_GetBloom cameraComponent_GetBloom = nullptr;
    Material_SetFloatValue material_SetFloatValue = nullptr;
    Material_SetVec4Value material_SetVec4Value = nullptr;
    Material_SetTextureValue material_SetTextureValue = nullptr;
    Texture_ReadPixelsRGBA8 texture_ReadPixelsRGBA8 = nullptr;
    Mesh_Create mesh_Create = nullptr;
    Mesh_Update mesh_Update = nullptr;
    Mesh_Delete mesh_Delete = nullptr;
    Texture_CreateRGBA8 texture_CreateRGBA8 = nullptr;
    Texture_UpdateRGBA8 texture_UpdateRGBA8 = nullptr;
    Texture_Delete texture_Delete = nullptr;
    Font_ReadData font_ReadData = nullptr;
    HostApiTable_Find hostApiTable_Find = nullptr;
    Console_Log console_Log = nullptr;
    UI_GetMainWindow ui_GetMainWindow = nullptr;
    UI_CreateWindow ui_CreateWindow = nullptr;
    UI_DestroyWindow ui_DestroyWindow = nullptr;
    UI_GetWindowViewport ui_GetWindowViewport = nullptr;
    UI_CreateTextureRGBA8 ui_CreateTextureRGBA8 = nullptr;
    UI_DestroyTexture ui_DestroyTexture = nullptr;
    UI_LoadStaticResource ui_LoadStaticResource = nullptr;
    UI_SubmitDrawList ui_SubmitDrawList = nullptr;
    UI_SetCursor ui_SetCursor = nullptr;
    UI_GetClipboardText ui_GetClipboardText = nullptr;
    UI_SetClipboardText ui_SetClipboardText = nullptr;
} // namespace ExternalCalls

namespace InternalCalls {
    void clear() {
        Bamboo::getScriptRegistry()->clear();
        Bamboo::UIRuntime::reset();
    }

    Handle instantiateScript(Handle entityHandle, const char *name) {
        return Bamboo::getScriptRegistry()->instantiate(Bamboo::EntityHandle(entityHandle), name);
    }

    void invokeUpdateAtScript(Handle entityHandle, Handle scriptID, float deltaTime) {
        Bamboo::Shared<Bamboo::Script> script =
            Bamboo::getScriptRegistry()->getEntryWithId(Bamboo::EntityHandle(entityHandle), scriptID).script;
        if (!script) { return; }
        script->update(deltaTime);
    }

    void invokeFixedUpdateAtScript(Handle entityHandle, Handle scriptID, float stepTime) {
        Bamboo::Shared<Bamboo::Script> script =
            Bamboo::getScriptRegistry()->getEntryWithId(Bamboo::EntityHandle(entityHandle), scriptID).script;
        if (!script) { return; }
        script->fixedUpdate(stepTime);
    }

    void invokeLateUpdateAtScript(Handle entityHandle, Handle scriptID, float deltaTime) {
        Bamboo::Shared<Bamboo::Script> script =
            Bamboo::getScriptRegistry()->getEntryWithId(Bamboo::EntityHandle(entityHandle), scriptID).script;
        if (!script) { return; }
        script->lateUpdate(deltaTime);
    }

    void invokeStartAtScript(Handle entityHandle, Handle scriptID) {
        Bamboo::Shared<Bamboo::Script> script =
            Bamboo::getScriptRegistry()->getEntryWithId(Bamboo::EntityHandle(entityHandle), scriptID).script;
        if (!script) { return; }
        script->start();
    }

    void invokeBeginCollisionTouch(Handle entityHandle, Handle scriptID, Handle collisionEntityHandle) {
        Bamboo::Shared<Bamboo::Script> script =
            Bamboo::getScriptRegistry()->getEntryWithId(Bamboo::EntityHandle(entityHandle), scriptID).script;
        if (!script) { return; }
        script->beginCollisionTouch(Bamboo::EntityHandle(collisionEntityHandle));
    }

    void invokeEndCollisionTouch(Handle entityHandle, Handle scriptID, Handle collisionEntityHandle) {
        Bamboo::Shared<Bamboo::Script> script =
            Bamboo::getScriptRegistry()->getEntryWithId(Bamboo::EntityHandle(entityHandle), scriptID).script;
        if (!script) { return; }
        script->endCollisionTouch(Bamboo::EntityHandle(collisionEntityHandle));
    }

    void invokeBeginSensorOverlap(Handle entityHandle, Handle scriptID, Handle sensorEntityHandle) {
        Bamboo::Shared<Bamboo::Script> script =
            Bamboo::getScriptRegistry()->getEntryWithId(Bamboo::EntityHandle(entityHandle), scriptID).script;
        if (!script) { return; }
        script->beginSensorOverlap(Bamboo::EntityHandle(sensorEntityHandle));
    }

    void invokeEndSensorOverlap(Handle entityHandle, Handle scriptID, Handle sensorEntityHandle) {
        Bamboo::Shared<Bamboo::Script> script =
            Bamboo::getScriptRegistry()->getEntryWithId(Bamboo::EntityHandle(entityHandle), scriptID).script;
        if (!script) { return; }
        script->endSensorOverlap(Bamboo::EntityHandle(sensorEntityHandle));
    }

    void setFieldValue(Handle entityHandle, Handle scriptID, FieldHandle fieldId, void *value) {
        Bamboo::getScriptRegistry()->setFieldValue(entityHandle, scriptID, fieldId, value);
    }

    void entityWillBeDeleted(Handle entityHandle) {
        Bamboo::getScriptRegistry()->entityWillBeDeleted(entityHandle);
    }

    Scripting::ScriptBundleInfo getManifest() {
        return ScriptClassMapper::getClassesManifest(Bamboo::getScriptRegistry()->getClasses());
    }
} // namespace InternalCalls

namespace {

    // The network runtime of the bundle rides on the hooks of Bamboo::Async (context/networking.md,
    // section 7): the sockets report before the completions of the same frame (HTTP answers,
    // resolve), Stop closes everything, and the HTTP client goes before the scheduler it posts to.
    void asyncUpdate(uint32_t maxCompletions, double maxMilliseconds) {
        Bamboo::Net::Internal::update();
        Bamboo::Async::Internal::update(maxCompletions, maxMilliseconds);
    }

    void asyncCancelAll() {
        Bamboo::Net::Internal::cancelAll();
        Bamboo::Async::Internal::cancelAllJobs();
    }

    void asyncWaitAll() {
        Bamboo::Net::Internal::shutdown();
        Bamboo::Async::Internal::waitAllJobs();
    }

    ScriptPluginApi s_pluginApi = {
        .abiVersion = Scripting::SCRIPT_ABI_VERSION,
        .clear = InternalCalls::clear,
        .instantiateScript = InternalCalls::instantiateScript,
        .setFieldValue = InternalCalls::setFieldValue,
        .invokeUpdateAtScript = InternalCalls::invokeUpdateAtScript,
        .invokeLateUpdateAtScript = InternalCalls::invokeLateUpdateAtScript,
        .invokeStartAtScript = InternalCalls::invokeStartAtScript,
        .invokeBeginCollisionTouch = InternalCalls::invokeBeginCollisionTouch,
        .invokeEndCollisionTouch = InternalCalls::invokeEndCollisionTouch,
        .invokeBeginSensorOverlap = InternalCalls::invokeBeginSensorOverlap,
        .invokeEndSensorOverlap = InternalCalls::invokeEndSensorOverlap,
        .entityWillBeDeleted = InternalCalls::entityWillBeDeleted,
        .getManifest = InternalCalls::getManifest,
        .ui_Update = Bamboo::UIRuntime::Internal::update,
        .ui_HandleEvent = Bamboo::UIRuntime::Internal::handleEvent,
        .ui_WindowClosed = Bamboo::UIRuntime::Internal::windowClosed,
        .async_Update = asyncUpdate,
        .async_CancelAll = asyncCancelAll,
        .async_WaitAll = asyncWaitAll,
        .invokeFixedUpdateAtScript = InternalCalls::invokeFixedUpdateAtScript,
    };

    void bindHostApi(const ScriptHostApi &hostApi) {
        using namespace ExternalCalls;
        application_Quit = hostApi.application_Quit;
        application_GetWidth = hostApi.application_GetWidth;
        application_GetHeight = hostApi.application_GetHeight;
        application_GetTime = hostApi.application_GetTime;
        application_IsCursorLocked = hostApi.application_IsCursorLocked;
        application_SetCursorLocked = hostApi.application_SetCursorLocked;
        application_GetPersistentDataPath = hostApi.application_GetPersistentDataPath;
        world_Load = hostApi.world_Load;
        world_FindByTag = hostApi.world_FindByTag;
        world_FindAllByTag = hostApi.world_FindAllByTag;
        world_CreateEntity = hostApi.world_CreateEntity;
        world_DestroyEntity = hostApi.world_DestroyEntity;
        worldLighting_SetAmbientColor = hostApi.worldLighting_SetAmbientColor;
        worldLighting_SetAmbientIntensity = hostApi.worldLighting_SetAmbientIntensity;
        input_IsKeyPressed = hostApi.input_IsKeyPressed;
        input_IsKeyJustPressed = hostApi.input_IsKeyJustPressed;
        input_IsMouseButtonPressed = hostApi.input_IsMouseButtonPressed;
        input_IsMouseButtonJustPressed = hostApi.input_IsMouseButtonJustPressed;
        input_GetMouseDeltaX = hostApi.input_GetMouseDeltaX;
        input_GetMouseDeltaY = hostApi.input_GetMouseDeltaY;
        input_GetTouchCount = hostApi.input_GetTouchCount;
        input_GetTouch = hostApi.input_GetTouch;
        entity_CreateComponent = hostApi.entity_CreateComponent;
        entity_HasComponent = hostApi.entity_HasComponent;
        entity_RemoveComponent = hostApi.entity_RemoveComponent;
        entity_GetName = hostApi.entity_GetName;
        transformComponent_GetPosition = hostApi.transformComponent_GetPosition;
        transformComponent_SetPosition = hostApi.transformComponent_SetPosition;
        transformComponent_Translate = hostApi.transformComponent_Translate;
        transformComponent_GetRotationEuler = hostApi.transformComponent_GetRotationEuler;
        transformComponent_SetRotationEuler = hostApi.transformComponent_SetRotationEuler;
        transformComponent_GetRotation = hostApi.transformComponent_GetRotation;
        transformComponent_SetRotation = hostApi.transformComponent_SetRotation;
        transformComponent_RotateEuler = hostApi.transformComponent_RotateEuler;
        transformComponent_GetScale = hostApi.transformComponent_GetScale;
        transformComponent_SetScale = hostApi.transformComponent_SetScale;
        spriteRendererComponent_GetColor = hostApi.spriteRendererComponent_GetColor;
        spriteRendererComponent_SetColor = hostApi.spriteRendererComponent_SetColor;
        spriteRendererComponent_GetMaterial = hostApi.spriteRendererComponent_GetMaterial;
        spriteRendererComponent_SetMaterial = hostApi.spriteRendererComponent_SetMaterial;
        spriteRendererComponent_GetTexture = hostApi.spriteRendererComponent_GetTexture;
        spriteRendererComponent_SetTexture = hostApi.spriteRendererComponent_SetTexture;
        spriteRendererComponent_SetCell = hostApi.spriteRendererComponent_SetCell;
        meshComponent_GetMesh = hostApi.meshComponent_GetMesh;
        meshComponent_SetMesh = hostApi.meshComponent_SetMesh;
        meshComponent_SetMaterial = hostApi.meshComponent_SetMaterial;
        directionalLightComponent_SetColor = hostApi.directionalLightComponent_SetColor;
        directionalLightComponent_SetIntensity = hostApi.directionalLightComponent_SetIntensity;
        directionalLightComponent_SetEnabled = hostApi.directionalLightComponent_SetEnabled;
        cameraComponent_SetPostprocessingEnabled = hostApi.cameraComponent_SetPostprocessingEnabled;
        cameraComponent_GetPostprocessingEnabled = hostApi.cameraComponent_GetPostprocessingEnabled;
        cameraComponent_SetBloom = hostApi.cameraComponent_SetBloom;
        cameraComponent_GetBloom = hostApi.cameraComponent_GetBloom;
        material_SetFloatValue = hostApi.material_SetFloatValue;
        material_SetVec4Value = hostApi.material_SetVec4Value;
        material_SetTextureValue = hostApi.material_SetTextureValue;
        texture_ReadPixelsRGBA8 = hostApi.texture_ReadPixelsRGBA8;
        mesh_Create = hostApi.mesh_Create;
        mesh_Update = hostApi.mesh_Update;
        mesh_Delete = hostApi.mesh_Delete;
        texture_CreateRGBA8 = hostApi.texture_CreateRGBA8;
        texture_UpdateRGBA8 = hostApi.texture_UpdateRGBA8;
        texture_Delete = hostApi.texture_Delete;
        font_ReadData = hostApi.font_ReadData;
        hostApiTable_Find = hostApi.hostApiTable_Find;
        console_Log = hostApi.console_Log;
        ui_GetMainWindow = hostApi.ui_GetMainWindow;
        ui_CreateWindow = hostApi.ui_CreateWindow;
        ui_DestroyWindow = hostApi.ui_DestroyWindow;
        ui_GetWindowViewport = hostApi.ui_GetWindowViewport;
        ui_CreateTextureRGBA8 = hostApi.ui_CreateTextureRGBA8;
        ui_DestroyTexture = hostApi.ui_DestroyTexture;
        ui_LoadStaticResource = hostApi.ui_LoadStaticResource;
        ui_SubmitDrawList = hostApi.ui_SubmitDrawList;
        ui_SetCursor = hostApi.ui_SetCursor;
        ui_GetClipboardText = hostApi.ui_GetClipboardText;
        ui_SetClipboardText = hostApi.ui_SetClipboardText;
    }

} // namespace

#if defined(_WIN32) || defined(_WIN64)
#    define LIB_EXPORT __declspec(dllexport)
#else
#    define LIB_EXPORT
#endif

extern "C" {

LIB_EXPORT const Panda::ScriptPluginApi *pandaLoadScriptBundle(const Panda::ScriptHostApi *hostApi) {
    if (!hostApi || hostApi->abiVersion != Panda::Scripting::SCRIPT_ABI_VERSION) { return nullptr; }
    Bamboo::Internal::registerMainThread();
    bindHostApi(*hostApi);
    Bamboo::UIRuntime::initialize();
    return &s_pluginApi;
}
}

} // namespace Panda
