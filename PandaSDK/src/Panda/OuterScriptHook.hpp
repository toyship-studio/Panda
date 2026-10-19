#pragma once

#include "Panda/ScriptEngine/ABI/ScriptApi.hpp"

namespace Panda {

namespace ExternalCalls {

    using Application_Quit = Scripting::ApplicationQuitFn;
    extern Application_Quit application_Quit;
    using Application_GetWidth = Scripting::ApplicationGetWidthFn;
    extern Application_GetWidth application_GetWidth;
    using Application_GetHeight = Scripting::ApplicationGetHeightFn;
    extern Application_GetHeight application_GetHeight;
    using Application_GetTime = Scripting::ApplicationGetTimeFn;
    extern Application_GetTime application_GetTime;
    using Application_IsCursorLocked = Scripting::ApplicationIsCursorLockedFn;
    extern Application_IsCursorLocked application_IsCursorLocked;
    using Application_SetCursorLocked = Scripting::ApplicationSetCursorLockedFn;
    extern Application_SetCursorLocked application_SetCursorLocked;
    using Application_GetPersistentDataPath = Scripting::ApplicationGetPersistentDataPathFn;
    extern Application_GetPersistentDataPath application_GetPersistentDataPath;

    using World_Load = Scripting::WorldLoadFn;
    extern World_Load world_Load;
    using World_FindByTag = Scripting::WorldFindByTagFn;
    extern World_FindByTag world_FindByTag;
    using World_FindAllByTag = Scripting::WorldFindAllByTagFn;
    extern World_FindAllByTag world_FindAllByTag;
    using World_CreateEntity = Scripting::WorldCreateEntityFn;
    extern World_CreateEntity world_CreateEntity;
    using World_DestroyEntity = Scripting::WorldDestroyEntityFn;
    extern World_DestroyEntity world_DestroyEntity;
    using WorldLighting_SetAmbientColor = Scripting::WorldLightingSetAmbientColorFn;
    extern WorldLighting_SetAmbientColor worldLighting_SetAmbientColor;
    using WorldLighting_SetAmbientIntensity = Scripting::WorldLightingSetAmbientIntensityFn;
    extern WorldLighting_SetAmbientIntensity worldLighting_SetAmbientIntensity;

    using Input_IsKeyPressed = Scripting::InputIsKeyPressedFn;
    extern Input_IsKeyPressed input_IsKeyPressed;
    using Input_IsKeyJustPressed = Scripting::InputIsKeyJustPressedFn;
    extern Input_IsKeyJustPressed input_IsKeyJustPressed;
    using Input_IsMouseButtonPressed = Scripting::InputIsMouseButtonPressedFn;
    extern Input_IsMouseButtonPressed input_IsMouseButtonPressed;
    using Input_IsMouseButtonJustPressed = Scripting::InputIsMouseButtonJustPressedFn;
    extern Input_IsMouseButtonJustPressed input_IsMouseButtonJustPressed;
    using Input_GetMouseDeltaX = Scripting::InputGetMouseDeltaXFn;
    extern Input_GetMouseDeltaX input_GetMouseDeltaX;
    using Input_GetMouseDeltaY = Scripting::InputGetMouseDeltaYFn;
    extern Input_GetMouseDeltaY input_GetMouseDeltaY;
    using Input_GetTouchCount = Scripting::InputGetTouchCountFn;
    extern Input_GetTouchCount input_GetTouchCount;
    using Input_GetTouch = Scripting::InputGetTouchFn;
    extern Input_GetTouch input_GetTouch;

    using Entity_CreateComponent = Scripting::EntityCreateComponentFn;
    extern Entity_CreateComponent entity_CreateComponent;
    using Entity_HasComponent = Scripting::EntityHasComponentFn;
    extern Entity_HasComponent entity_HasComponent;
    using Entity_RemoveComponent = Scripting::EntityRemoveComponentFn;
    extern Entity_RemoveComponent entity_RemoveComponent;
    using Entity_GetName = Scripting::EntityGetNameFn;
    extern Entity_GetName entity_GetName;

    using TransformComponent_GetPosition = Scripting::TransformComponentGetPositionFn;
    extern TransformComponent_GetPosition transformComponent_GetPosition;
    using TransformComponent_SetPosition = Scripting::TransformComponentSetPositionFn;
    extern TransformComponent_SetPosition transformComponent_SetPosition;
    using TransformComponent_Translate = Scripting::TransformComponentTranslateFn;
    extern TransformComponent_Translate transformComponent_Translate;
    using TransformComponent_GetRotationEuler = Scripting::TransformComponentGetRotationEulerFn;
    extern TransformComponent_GetRotationEuler transformComponent_GetRotationEuler;
    using TransformComponent_SetRotationEuler = Scripting::TransformComponentSetRotationEulerFn;
    extern TransformComponent_SetRotationEuler transformComponent_SetRotationEuler;
    using TransformComponent_GetRotation = Scripting::TransformComponentGetRotationFn;
    extern TransformComponent_GetRotation transformComponent_GetRotation;
    using TransformComponent_SetRotation = Scripting::TransformComponentSetRotationFn;
    extern TransformComponent_SetRotation transformComponent_SetRotation;
    using TransformComponent_RotateEuler = Scripting::TransformComponentRotateEulerFn;
    extern TransformComponent_RotateEuler transformComponent_RotateEuler;
    using TransformComponent_GetScale = Scripting::TransformComponentGetScaleFn;
    extern TransformComponent_GetScale transformComponent_GetScale;
    using TransformComponent_SetScale = Scripting::TransformComponentSetScaleFn;
    extern TransformComponent_SetScale transformComponent_SetScale;

    using SpriteRendererComponent_GetColor = Scripting::SpriteRendererComponentGetColorFn;
    extern SpriteRendererComponent_GetColor spriteRendererComponent_GetColor;
    using SpriteRendererComponent_SetColor = Scripting::SpriteRendererComponentSetColorFn;
    extern SpriteRendererComponent_SetColor spriteRendererComponent_SetColor;
    using SpriteRendererComponent_GetMaterial = Scripting::SpriteRendererComponentGetMaterialFn;
    extern SpriteRendererComponent_GetMaterial spriteRendererComponent_GetMaterial;
    using SpriteRendererComponent_SetMaterial = Scripting::SpriteRendererComponentSetMaterialFn;
    extern SpriteRendererComponent_SetMaterial spriteRendererComponent_SetMaterial;
    using SpriteRendererComponent_GetTexture = Scripting::SpriteRendererComponentGetTextureFn;
    extern SpriteRendererComponent_GetTexture spriteRendererComponent_GetTexture;
    using SpriteRendererComponent_SetTexture = Scripting::SpriteRendererComponentSetTextureFn;
    extern SpriteRendererComponent_SetTexture spriteRendererComponent_SetTexture;
    using SpriteRendererComponent_SetCell = Scripting::SpriteRendererComponentSetCellFn;
    extern SpriteRendererComponent_SetCell spriteRendererComponent_SetCell;

    using MeshComponent_GetMesh = Scripting::MeshComponentGetMeshFn;
    extern MeshComponent_GetMesh meshComponent_GetMesh;
    using MeshComponent_SetMesh = Scripting::MeshComponentSetMeshFn;
    extern MeshComponent_SetMesh meshComponent_SetMesh;
    using MeshComponent_SetMaterial = Scripting::MeshComponentSetMaterialFn;
    extern MeshComponent_SetMaterial meshComponent_SetMaterial;

    using DirectionalLightComponent_SetColor = Scripting::DirectionalLightComponentSetColorFn;
    extern DirectionalLightComponent_SetColor directionalLightComponent_SetColor;
    using DirectionalLightComponent_SetIntensity = Scripting::DirectionalLightComponentSetIntensityFn;
    extern DirectionalLightComponent_SetIntensity directionalLightComponent_SetIntensity;
    using DirectionalLightComponent_SetEnabled = Scripting::DirectionalLightComponentSetEnabledFn;
    extern DirectionalLightComponent_SetEnabled directionalLightComponent_SetEnabled;

    using CameraComponent_SetPostprocessingEnabled = Scripting::CameraComponentSetPostprocessingEnabledFn;
    extern CameraComponent_SetPostprocessingEnabled cameraComponent_SetPostprocessingEnabled;
    using CameraComponent_GetPostprocessingEnabled = Scripting::CameraComponentGetPostprocessingEnabledFn;
    extern CameraComponent_GetPostprocessingEnabled cameraComponent_GetPostprocessingEnabled;
    using CameraComponent_SetBloom = Scripting::CameraComponentSetBloomFn;
    extern CameraComponent_SetBloom cameraComponent_SetBloom;
    using CameraComponent_GetBloom = Scripting::CameraComponentGetBloomFn;
    extern CameraComponent_GetBloom cameraComponent_GetBloom;

    using Material_SetFloatValue = Scripting::MaterialSetFloatValueFn;
    extern Material_SetFloatValue material_SetFloatValue;
    using Material_SetVec4Value = Scripting::MaterialSetVec4ValueFn;
    extern Material_SetVec4Value material_SetVec4Value;
    using Material_SetTextureValue = Scripting::MaterialSetTextureValueFn;
    extern Material_SetTextureValue material_SetTextureValue;
    using Texture_ReadPixelsRGBA8 = Scripting::TextureReadPixelsRGBA8Fn;
    extern Texture_ReadPixelsRGBA8 texture_ReadPixelsRGBA8;

    using Mesh_Create = Scripting::MeshCreateFn;
    extern Mesh_Create mesh_Create;
    using Mesh_Update = Scripting::MeshUpdateFn;
    extern Mesh_Update mesh_Update;
    using Mesh_Delete = Scripting::MeshDeleteFn;
    extern Mesh_Delete mesh_Delete;

    using Texture_CreateRGBA8 = Scripting::TextureCreateRGBA8Fn;
    extern Texture_CreateRGBA8 texture_CreateRGBA8;
    using Texture_UpdateRGBA8 = Scripting::TextureUpdateRGBA8Fn;
    extern Texture_UpdateRGBA8 texture_UpdateRGBA8;
    using Texture_Delete = Scripting::TextureDeleteFn;
    extern Texture_Delete texture_Delete;

    // Named tables of host contributions (plugins): by name and version, nullptr — no contribution.
    using HostApiTable_Find = Scripting::HostApiTableFindFn;
    extern HostApiTable_Find hostApiTable_Find;

    using Console_Log = Scripting::ConsoleLogFn;
    extern Console_Log console_Log;

    using UI_GetMainWindow = Scripting::UIGetMainWindowFn;
    extern UI_GetMainWindow ui_GetMainWindow;
    using UI_CreateWindow = Scripting::UICreateWindowFn;
    extern UI_CreateWindow ui_CreateWindow;
    using UI_DestroyWindow = Scripting::UIDestroyWindowFn;
    extern UI_DestroyWindow ui_DestroyWindow;
    using UI_GetWindowViewport = Scripting::UIGetWindowViewportFn;
    extern UI_GetWindowViewport ui_GetWindowViewport;
    using UI_CreateTextureRGBA8 = Scripting::UICreateTextureRGBA8Fn;
    extern UI_CreateTextureRGBA8 ui_CreateTextureRGBA8;
    using UI_DestroyTexture = Scripting::UIDestroyTextureFn;
    extern UI_DestroyTexture ui_DestroyTexture;
    using UI_LoadStaticResource = Scripting::UILoadStaticResourceFn;
    extern UI_LoadStaticResource ui_LoadStaticResource;
    using UI_SubmitDrawList = Scripting::UISubmitDrawListFn;
    extern UI_SubmitDrawList ui_SubmitDrawList;
    using UI_SetCursor = Scripting::UISetCursorFn;
    extern UI_SetCursor ui_SetCursor;
    using UI_GetClipboardText = Scripting::UIGetClipboardTextFn;
    extern UI_GetClipboardText ui_GetClipboardText;
    using UI_SetClipboardText = Scripting::UISetClipboardTextFn;
    extern UI_SetClipboardText ui_SetClipboardText;

} // namespace ExternalCalls

} // namespace Panda
