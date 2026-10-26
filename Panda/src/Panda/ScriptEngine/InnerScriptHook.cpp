#include "Panda/ScriptEngine/InnerScriptHook.hpp"
#include "Panda/UI/PandaUIImpl.hpp"
#include "Panda/Application/Application.hpp"
#include "Panda/GameLogic/Input.hpp"
#include "Panda/GameLogic/GameContext.hpp"
#include "Rain/TypeName.hpp"
#include "Panda/GameLogic/World.hpp"
#include "Panda/Assets/FontAsset.hpp"
#include "Panda/Assets/StaticResources.hpp"
#include "Panda/Extensions/Extensions.hpp"

#include <Fern/Fern.hpp>
#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

namespace Panda {

std::unordered_map<UUID, AssetRef<Asset>> s_scriptCreatedAssets;

// The Handle comes from a user script: a broken reference (the entity was deleted at runtime,
// the field is not set, the id is from another world) must not crash the engine. An invalid
// Entity is returned + a one-time error in the console for each id — the call becomes a no-op.
Entity resolveScriptEntity(Handle entityHandle) {
    World *world = GameContext::getCurrentWorld();
    Entity entity = world ? world->getById(entityHandle) : Entity();
    if (!entity.isValid()) {
        static std::unordered_set<uint32_t> reported;
        if (reported.insert((uint32_t)entityHandle).second) {
            LOG_ERROR("SCRIPT CALL WITH INVALID ENTITY %u", (uint32_t)entityHandle);
            if (Foundation::EditorLogger::isInitialized()) {
                LOG_ERROR_EDITOR("Script call with invalid entity %u", (uint32_t)entityHandle);
            }
        }
    }
    return entity;
}

namespace InternalCalls {

    /// APPLICATION

    void application_Quit() {
        GameContext::requestQuit(); // the editor intercepts it (Stop), the runtime closes the application
    }

    uint32_t application_GetWidth() {
        return Panda::Application::get()->getMainWindow()->getSize().width;
    }

    uint32_t application_GetHeight() {
        return Panda::Application::get()->getMainWindow()->getSize().height;
    }

    float application_GetTime() {
        return Panda::Application::get()->getTime();
    }

    bool application_IsCursorLocked() {
        return Fern::isCursorLocked();
    }

    void application_SetCursorLocked(bool locked) {
        Fern::setCursorLocked(locked);
    }

    uint32_t application_GetPersistentDataPath(void *destination, uint32_t destinationSize) {
        const std::string path = GameContext::getPersistentDataPath().string();
        const uint32_t length = static_cast<uint32_t>(path.size());
        if (destination != nullptr && destinationSize >= length + 1) {
            std::memcpy(destination, path.c_str(), length + 1);
        }
        return length;
    }

    /// INPUT

    bool input_IsKeyPressed(int key) {
        return Input::isKeyPressed((Fern::Key)key);
    }

    bool input_IsKeyJustPressed(int key) {
        return Input::isKeyJustPressed((Fern::Key)key);
    }

    bool input_IsMouseButtonPressed(int key) {
        return Input::isMouseButtonPressed((Fern::MouseButton)key);
    }

    bool input_IsMouseButtonJustPressed(int key) {
        return Input::isMouseButtonJustPressed((Fern::MouseButton)key);
    }

    double input_GetMouseDeltaX() {
        return Input::getMouseDeltaX();
    }

    double input_GetMouseDeltaY() {
        return Input::getMouseDeltaY();
    }

    int input_GetTouchCount() {
        return Input::touchCount();
    }

    bool input_GetTouch(int index, int *id, float *x, float *y) {
        if (index < 0 || index >= Input::touchCount()) { return false; }

        Input::Touch touch = Input::getTouch(index);
        if (id) { *id = touch.id; }
        if (x) { *x = touch.x; }
        if (y) { *y = touch.y; }
        return true;
    }

    /// WORLD

    void world_Load(Handle worldHandle) {
        GameContext::requestWorldLoad(AssetId(worldHandle));
    }

    Handle world_FindByTag(const char *tag) {
        Entity entity = GameContext::getCurrentWorld()->findByTag(tag);
        return entity.isValid() ? (Handle)entity.getId() : 0; // not found — 0, not a getId assert
    }

    uint32_t world_FindAllByTag(const char *tag, Handle *entityHandles, uint32_t capacity) {
        std::vector<Entity> entities = GameContext::getCurrentWorld()->findAllByTag(tag);
        const uint32_t count = static_cast<uint32_t>(entities.size());
        if (!entityHandles || capacity == 0) { return count; }

        const uint32_t writeCount = std::min(count, capacity);
        for (uint32_t i = 0; i < writeCount; ++i) {
            entityHandles[i] = entities[i].getId();
        }
        return count;
    }

    Handle world_CreateEntity(const char *tag) {
        Entity entity = GameContext::getCurrentWorld()->instantiateEntity();
        entity.setName(tag);
#ifdef PND_EDITOR
        entity.sortWorld();
#endif
        return entity.getId();
    }

    void world_DestroyEntity(Handle id) {
        Entity entity = resolveScriptEntity(id);
        if (!entity.isValid()) { return; }
        GameContext::getCurrentWorld()->destroy(entity); // a running world: after the callbacks
    }

    void worldLighting_SetAmbientColor(float r, float g, float b, float a) {
        World *world = GameContext::getCurrentWorld();
        if (!world) { return; }
        world->getLightingSettings().ambientColor = Color(r, g, b, a);
    }

    void worldLighting_SetAmbientIntensity(float intensity) {
        World *world = GameContext::getCurrentWorld();
        if (!world) { return; }
        world->getLightingSettings().ambientIntensity = intensity;
    }

    /// ENTITY

    // Components by name — the component type registry (Extensions::components): the key of the
    // type in .world; the modules' types work the same way as the core's.
    // A component type by its registry name from a script; an unknown name (a module that is not
    // in the game, a typo) is a one-time error in the console, the call becomes a no-op.
    const ComponentType *componentTypeNamed(const char *type) {
        if (type == nullptr) { return nullptr; }
        const ComponentType *componentType = GameContext::getExtensions().components.find(type);
        if (componentType == nullptr) {
            static std::unordered_set<std::string> reported;
            if (reported.insert(type).second) {
                LOG_ERROR("SCRIPT CALL WITH UNKNOWN COMPONENT TYPE %s", type);
                if (Foundation::EditorLogger::isInitialized()) {
                    LOG_ERROR_EDITOR("Script call with unknown component type %s", type);
                }
            }
        }
        return componentType;
    }

    void entity_CreateComponent(Handle entityHandle, const char *type) {
        Entity entity = resolveScriptEntity(entityHandle);
        if (!entity.isValid()) { return; }
        const ComponentType *componentType = componentTypeNamed(type);
        if (componentType == nullptr || componentType->create == nullptr) { return; }
        if (entity.hasComponent(componentType->typeId)) { return; }
        componentType->create(entity);
    }

    bool entity_HasComponent(Handle entityHandle, const char *type) {
        Entity entity = resolveScriptEntity(entityHandle);
        if (!entity.isValid()) { return false; }
        const ComponentType *componentType = componentTypeNamed(type);
        return componentType != nullptr && entity.hasComponent(componentType->typeId);
    }

    void entity_RemoveComponent(Handle entityHandle, const char *type) {
        Entity entity = resolveScriptEntity(entityHandle);
        if (!entity.isValid()) { return; }
        const ComponentType *componentType = componentTypeNamed(type);
        if (componentType == nullptr || componentType->remove == nullptr) { return; }
        if (!entity.hasComponent(componentType->typeId)) {
            PND_ASSERT_F(false, "ENTITY %d DOES NOT HAVE COMPONENT %s", (uint32_t)entityHandle, type);
            return;
        }
        componentType->remove(entity);
    }

    const char *entity_GetName(Handle entityId) {
        Entity entity = resolveScriptEntity(entityId);
        if (!entity.isValid()) { return ""; }
        return entity.getName().c_str();
    }

    /// TRANSFORM COMPONENT

    void transformComponent_GetPosition(Handle entityHandle, float *x, float *y, float *z) {
        Entity entity = resolveScriptEntity(entityHandle);
        if (!entity.isValid()) {
            *x = 0.f;
            *y = 0.f;
            *z = 0.f;
            return;
        }
        TransformComponent &transformComponent = entity.getTransform();
        glm::vec3 position = transformComponent.getPosition();
        *x = position.x;
        *y = position.y;
        *z = position.z;
    }

    void transformComponent_SetPosition(Handle entityHandle, float x, float y, float z) {
        Entity entity = resolveScriptEntity(entityHandle);
        if (!entity.isValid()) { return; }
        TransformComponent &transformComponent = entity.getTransform();
        transformComponent.setPosition({x, y, z});
        entity.transformUpdated();
    }

    void transformComponent_Translate(Handle entityHandle, float x, float y, float z) {
        Entity entity = resolveScriptEntity(entityHandle);
        if (!entity.isValid()) { return; }
        TransformComponent &transformComponent = entity.getTransform();
        transformComponent.translate({x, y, z});
        entity.transformUpdated();
    }

    void transformComponent_GetRotationEuler(Handle entityHandle, float *x, float *y, float *z) {
        Entity entity = resolveScriptEntity(entityHandle);
        if (!entity.isValid()) {
            *x = 0.f;
            *y = 0.f;
            *z = 0.f;
            return;
        }
        TransformComponent &transformComponent = entity.getTransform();
        glm::vec3 rotation = transformComponent.getRotationEuler();
        *x = rotation.x;
        *y = rotation.y;
        *z = rotation.z;
    }

    void transformComponent_SetRotationEuler(Handle entityHandle, float x, float y, float z) {
        Entity entity = resolveScriptEntity(entityHandle);
        if (!entity.isValid()) { return; }
        TransformComponent &transformComponent = entity.getTransform();
        transformComponent.setRotationEuler({x, y, z});
        entity.transformUpdated();
    }

    void transformComponent_RotateEuler(Handle entityHandle, float x, float y, float z) {
        Entity entity = resolveScriptEntity(entityHandle);
        if (!entity.isValid()) { return; }
        TransformComponent &transformComponent = entity.getTransform();
        transformComponent.rotateEuler({x, y, z});
        entity.transformUpdated();
    }

    void transformComponent_GetRotation(Handle entityHandle, float *w, float *x, float *y, float *z) {
        Entity entity = resolveScriptEntity(entityHandle);
        if (!entity.isValid()) {
            *w = 0.f;
            *x = 0.f;
            *y = 0.f;
            *z = 0.f;
            return;
        }
        TransformComponent &transformComponent = entity.getTransform();
        glm::quat rotation = transformComponent.getRotation();
        *w = rotation.w;
        *x = rotation.x;
        *y = rotation.y;
        *z = rotation.z;
    }

    void transformComponent_SetRotation(Handle entityHandle, float w, float x, float y, float z) {
        Entity entity = resolveScriptEntity(entityHandle);
        if (!entity.isValid()) { return; }
        TransformComponent &transformComponent = entity.getTransform();
        transformComponent.setRotation({w, x, y, z});
        entity.transformUpdated();
    }

    void transformComponent_GetScale(Handle entityHandle, float *x, float *y, float *z) {
        Entity entity = resolveScriptEntity(entityHandle);
        if (!entity.isValid()) {
            *x = 0.f;
            *y = 0.f;
            *z = 0.f;
            return;
        }
        TransformComponent &transformComponent = entity.getTransform();
        glm::vec3 scale = transformComponent.getScale();
        *x = scale.x;
        *y = scale.y;
        *z = scale.z;
    }

    void transformComponent_SetScale(Handle entityHandle, float x, float y, float z) {
        Entity entity = resolveScriptEntity(entityHandle);
        if (!entity.isValid()) { return; }
        TransformComponent &transformComponent = entity.getTransform();
        transformComponent.setScale({x, y, z});
        // The scale changes the shape size — a full rebuild of the collider, not a teleport.
        entity.scaleUpdated();
    }

    /// SPRITE RENDERER COMPONENT

    void spriteRendererComponent_GetColor(Handle entityHandle, float *r, float *g, float *b, float *a) {
        Entity entity = resolveScriptEntity(entityHandle);
        if (!entity.isValid()) {
            *r = 0.f;
            *g = 0.f;
            *b = 0.f;
            *a = 0.f;
            return;
        }
        SpriteRendererComponent &sr = entity.getComponent<SpriteRendererComponent>();
        *r = sr.color.r;
        *g = sr.color.g;
        *b = sr.color.b;
        *a = sr.color.a;
    }

    void spriteRendererComponent_SetColor(Handle entityHandle, float r, float g, float b, float a) {
        Entity entity = resolveScriptEntity(entityHandle);
        if (!entity.isValid()) { return; }
        SpriteRendererComponent &sr = entity.getComponent<SpriteRendererComponent>();
        sr.color = Color(r, g, b, a);
    }

    void spriteRendererComponent_GetMaterial(Handle entityHandle, Handle *materialId) {
        Entity entity = resolveScriptEntity(entityHandle);
        if (!entity.isValid()) {
            *materialId = 0;
            return;
        }
        SpriteRendererComponent &sr = entity.getComponent<SpriteRendererComponent>();
        *materialId = sr.material.getId();
    }

    void spriteRendererComponent_SetMaterial(Handle entityHandle, Handle materialId) {
        Entity entity = resolveScriptEntity(entityHandle);
        if (!entity.isValid()) { return; }
        SpriteRendererComponent &sr = entity.getComponent<SpriteRendererComponent>();
        sr.material = AssetRef<Asset>(materialId);
    }

    void spriteRendererComponent_GetTexture(Handle entityHandle, Handle *textureId) {
        Entity entity = resolveScriptEntity(entityHandle);
        if (!entity.isValid()) {
            *textureId = 0;
            return;
        }
        SpriteRendererComponent &sr = entity.getComponent<SpriteRendererComponent>();
        *textureId = sr.texture.getId();
    }

    void spriteRendererComponent_SetTexture(Handle entityHandle, Handle textureId) {
        if (textureId) {
            auto textureRef = AssetRef<TextureAsset>(textureId);
            if (!textureRef.get()) {
                LOG_ERROR("TEXTURE %u NOT FOUND", textureRef.getId());
                LOG_ERROR_EDITOR("TEXTURE %u NOT FOUND", textureRef.getId());
                return;
            }
        }
        Entity entity = resolveScriptEntity(entityHandle);
        if (!entity.isValid()) { return; }
        SpriteRendererComponent &sr = entity.getComponent<SpriteRendererComponent>();
        sr.texture = AssetRef<Asset>(textureId);
    }

    void spriteRendererComponent_SetCell(Handle entityHandle, int cols, int rows, int index) {
        Entity entity = resolveScriptEntity(entityHandle);
        if (!entity.isValid()) { return; }
        SpriteRendererComponent &sr = entity.getComponent<SpriteRendererComponent>();
        sr.cols = cols;
        sr.rows = rows;
        sr.index = index;
    }

    /// MESH COMPONENT

    void meshComponent_GetMesh(Handle entityHandle, Handle *meshHandle) {
        Entity entity = resolveScriptEntity(entityHandle);
        if (!entity.isValid()) {
            *meshHandle = 0;
            return;
        }
        RuntimeMeshComponent &mc = entity.getComponent<RuntimeMeshComponent>();
        *meshHandle = mc.runtimeMesh.getId();
    }

    void meshComponent_SetMesh(Handle entityHandle, Handle meshHandle) {
        Entity entity = resolveScriptEntity(entityHandle);
        if (!entity.isValid()) { return; }
        RuntimeMeshComponent &mc = entity.getComponent<RuntimeMeshComponent>();
        mc.runtimeMesh = AssetRef<Asset>(meshHandle);
    }

    void meshComponent_SetMaterial(Handle entityHandle, Handle materialHandle) {
        auto materialRef = AssetRef<MaterialAsset>(materialHandle);
        if (!materialRef) {
            LOG_ERROR("MATERIAL %u NOT FOUND", materialRef.getId());
            LOG_ERROR_EDITOR("MATERIAL %u NOT FOUND", materialRef.getId());
            return;
        }
        Entity entity = resolveScriptEntity(entityHandle);
        if (!entity.isValid()) { return; }
        RuntimeMeshComponent &mc = entity.getComponent<RuntimeMeshComponent>();
        mc.material = materialRef.asBaseAsset();
    }

    /// DIRECTIONAL LIGHT COMPONENT

    void directionalLightComponent_SetColor(Handle entityHandle, float r, float g, float b, float a) {
        Entity entity = resolveScriptEntity(entityHandle);
        if (!entity.isValid()) { return; }
        DirectionalLightComponent &light = entity.getComponent<DirectionalLightComponent>();
        light.color = Color(r, g, b, a);
    }

    void directionalLightComponent_SetIntensity(Handle entityHandle, float intensity) {
        Entity entity = resolveScriptEntity(entityHandle);
        if (!entity.isValid()) { return; }
        DirectionalLightComponent &light = entity.getComponent<DirectionalLightComponent>();
        light.intensity = intensity;
    }

    void directionalLightComponent_SetEnabled(Handle entityHandle, bool enabled) {
        Entity entity = resolveScriptEntity(entityHandle);
        if (!entity.isValid()) { return; }
        DirectionalLightComponent &light = entity.getComponent<DirectionalLightComponent>();
        light.isEnabled = enabled;
    }

    /// CAMERA COMPONENT

    void cameraComponent_SetPostprocessingEnabled(Handle entityHandle, uint32_t enabled) {
        Entity entity = resolveScriptEntity(entityHandle);
        if (!entity.isValid()) { return; }
        if (!entity.hasComponent<CameraComponent>()) { return; }
        entity.getComponent<CameraComponent>().postprocessingEnabled = enabled != 0;
    }

    void cameraComponent_GetPostprocessingEnabled(Handle entityHandle, uint32_t *enabled) {
        Entity entity = resolveScriptEntity(entityHandle);
        if (!entity.isValid()) {
            *enabled = 0;
            return;
        }
        *enabled = entity.hasComponent<CameraComponent>() &&
                           entity.getComponent<CameraComponent>().postprocessingEnabled
                       ? 1
                       : 0;
    }

    void cameraComponent_SetBloom(Handle entityHandle, uint32_t enabled, float intensity, float radius) {
        Entity entity = resolveScriptEntity(entityHandle);
        if (!entity.isValid()) { return; }
        if (!entity.hasComponent<CameraComponent>()) { return; }
        CameraComponent &camera = entity.getComponent<CameraComponent>();
        camera.bloomEnabled = enabled != 0;
        camera.bloomIntensity = intensity;
        camera.bloomRadius = radius;
    }

    void cameraComponent_GetBloom(Handle entityHandle, uint32_t *enabled, float *intensity, float *radius) {
        Entity entity = resolveScriptEntity(entityHandle);
        if (!entity.isValid()) {
            *enabled = 0;
            *intensity = 0.f;
            *radius = 0.f;
            return;
        }
        if (!entity.hasComponent<CameraComponent>()) {
            *enabled = 0;
            *intensity = 0.f;
            *radius = 0.f;
            return;
        }
        CameraComponent &camera = entity.getComponent<CameraComponent>();
        *enabled = camera.bloomEnabled ? 1 : 0;
        *intensity = camera.bloomIntensity;
        *radius = camera.bloomRadius;
    }

    /// MATERIAL

    void material_SetFloatValue(Handle materialHandle, const char *name, float value) {
        auto material = AssetRef<MaterialAsset>(materialHandle);
        material->setFieldValue(name, value);
    }

    void material_SetVec4Value(Handle materialHandle, const char *name, float r, float g, float b, float a) {
        auto material = AssetRef<MaterialAsset>(materialHandle);
        Vec4 value(r, g, b, a);
        material->setFieldValue(name, value);
    }

    void material_SetTextureValue(Handle materialHandle, const char *name, Handle value) {
        auto material = AssetRef<MaterialAsset>(materialHandle);
        auto texture = AssetRef<Asset>(value);
        material->setFieldValue(name, texture);
    }

    /// TEXTURE

    uint32_t texture_ReadPixelsRGBA8(
        Handle textureHandle, uint32_t *width, uint32_t *height, void *destination, uint32_t destinationSize
    ) {
        if (width) { *width = 0; }
        if (height) { *height = 0; }

        AssetHandler *assetHandler = GameContext::getAssetHandler();
        if (!assetHandler || !textureHandle) { return 0; }

        std::vector<unsigned char> pixels;
        uint32_t textureWidth = 0;
        uint32_t textureHeight = 0;
        if (!assetHandler->readTexturePixelsRGBA8(textureHandle, &textureWidth, &textureHeight, pixels)) { return 0; }
        if (pixels.size() > UINT32_MAX) { return 0; }

        if (width) { *width = textureWidth; }
        if (height) { *height = textureHeight; }
        const uint32_t requiredSize = static_cast<uint32_t>(pixels.size());
        if (destination && destinationSize >= requiredSize) {
            std::memcpy(destination, pixels.data(), requiredSize);
        }
        return requiredSize;
    }

    /// FONT

    uint32_t font_ReadData(Handle fontHandle, void *destination, uint32_t destinationSize) {
        if (!fontHandle || !GameContext::getAssetHandler()) { return 0; }
        AssetRef<FontAsset> ref{AssetId(fontHandle)};
        const FontAsset *font = ref.get();
        if (!font || font->isEmpty() || font->data().size() > UINT32_MAX) { return 0; }
        const uint32_t size = static_cast<uint32_t>(font->data().size());
        if (destination && destinationSize >= size) { std::memcpy(destination, font->data().data(), size); }
        return size;
    }

    /// MESH

    void mesh_Create(Handle *handle) {
        AssetHandler *assetHandler = GameContext::getAssetHandler();
        UUID id;
        auto meshRef = assetHandler->createAsset<RuntimeMeshAsset>(id);
        *handle = id;
        s_scriptCreatedAssets[id] = meshRef.asBaseAsset();
    }

    void mesh_Update(
        Handle handle, const void *vertices, uint32_t verticesCount, const void *indices, uint32_t indicesCount
    ) {
        auto meshRef = AssetRef<RuntimeMeshAsset>(handle);
        if (!meshRef) {
            LOG_ERROR("MESH %u NOT FOUND", meshRef.getId());
            LOG_ERROR_EDITOR("MESH %u NOT FOUND", meshRef.getId());
            return;
        }
        uint32_t verticesSize = sizeof(Vertex) * verticesCount;
        Foundation::Memory vertexBuffer = Foundation::Memory::copying(vertices, verticesSize);
        uint32_t indicesSize = sizeof(uint32_t) * indicesCount;
        Foundation::Memory indexBuffer = Foundation::Memory::copying(indices, indicesSize);
        MeshData meshData(StaticResources::generalVertexLayout, vertexBuffer, verticesSize, indexBuffer, indicesCount);
        meshData.vertexStride = sizeof(Vertex); // the position comes first — AABB for culling
        meshRef->update(meshData);
    }

    void mesh_Delete(Handle handle) {
        if (!s_scriptCreatedAssets.contains(handle)) {
            LOG_ERROR("MESH %u WAS NOT CREATED BY SCRIPTING", handle);
            LOG_ERROR_EDITOR("MESH %u WAS NOT CREATED BY SCRIPTING", handle);
            return;
        }
        s_scriptCreatedAssets.erase(handle);
    }

    /// RUNTIME TEXTURE (a mirror of the runtime meshes: dynamic textures of games)

    void texture_CreateRGBA8(uint32_t width, uint32_t height, Handle *handle) {
        AssetHandler *assetHandler = GameContext::getAssetHandler();
        UUID id;
        Miren::TextureCreate create;
        create.m_format = Miren::TextureFormat::RGBA8;
        create.m_width = static_cast<uint16_t>(width);
        create.m_height = static_cast<uint16_t>(height);
        create.m_minFiltering = Miren::LINEAR;
        create.m_magFiltering = Miren::LINEAR;
        create.m_wrap = Miren::CLAMP;
        create.m_numMips = 0;
        const size_t sizeBytes = static_cast<size_t>(width) * height * 4;
        Foundation::Memory pixels = Foundation::Memory::alloc(sizeBytes);
        std::memset(pixels.data, 0, sizeBytes);
        create.m_data = pixels;
        auto textureRef = assetHandler->createAsset<TextureAsset>(id, create);
        *handle = id;
        s_scriptCreatedAssets[id] = textureRef.asBaseAsset();
    }

    void texture_UpdateRGBA8(Handle handle, const void *pixels, uint32_t sizeBytes) {
        auto textureRef = AssetRef<TextureAsset>(handle);
        if (!textureRef) {
            LOG_ERROR("TEXTURE %u NOT FOUND", handle);
            LOG_ERROR_EDITOR("TEXTURE %u NOT FOUND", handle);
            return;
        }
        const Size textureSize = textureRef->getSize();
        const uint32_t expectedBytes =
            static_cast<uint32_t>(textureSize.width) * static_cast<uint32_t>(textureSize.height) * 4;
        if (sizeBytes != expectedBytes) {
            LOG_ERROR("TEXTURE %u UPDATE SIZE MISMATCH (%u != %u)", handle, sizeBytes, expectedBytes);
            LOG_ERROR_EDITOR("TEXTURE %u UPDATE SIZE MISMATCH (%u != %u)", handle, sizeBytes, expectedBytes);
            return;
        }
        Miren::updateTexture(textureRef->getMirenHandle(), Foundation::Memory::copying(pixels, sizeBytes));
    }

    void texture_Delete(Handle handle) {
        if (!s_scriptCreatedAssets.contains(handle)) {
            LOG_ERROR("TEXTURE %u WAS NOT CREATED BY SCRIPTING", handle);
            LOG_ERROR_EDITOR("TEXTURE %u WAS NOT CREATED BY SCRIPTING", handle);
            return;
        }
        s_scriptCreatedAssets.erase(handle);
    }

    /// HOST API TABLES — named tables of contributions, see ScriptHostApis.hpp.

    const void *hostApiTable_Find(const char *name, uint32_t version) {
        return GameContext::getExtensions().scriptHostApis.find(name != nullptr ? name : "", version);
    }

    /// CONSOLE

    void console_Log(int type, const char *message) {
        const char *safeMessage = message ? message : "";
        if (type == 0) {
            if (Foundation::EditorLogger::isInitialized()) { LOG_INFO_EDITOR("%s", safeMessage); }
            LOG_INFO("%s", safeMessage);
        } else if (type == 1) {
            if (Foundation::EditorLogger::isInitialized()) { LOG_WARN_EDITOR("%s", safeMessage); }
            LOG_WARN("%s", safeMessage);
        } else if (type == 2) {
            if (Foundation::EditorLogger::isInitialized()) { LOG_ERROR_EDITOR("%s", safeMessage); }
            LOG_ERROR("%s", safeMessage);
        }
    }
} // namespace InternalCalls

namespace ExternalCalls {
    Clear clear = nullptr;
    InstantiateScript instantiateScript = nullptr;
    SetFieldValue setFieldValue = nullptr;
    InvokeUpdateAtScript invokeUpdateAtScript = nullptr;
    InvokeFixedUpdateAtScript invokeFixedUpdateAtScript = nullptr;
    InvokeLateUpdateAtScript invokeLateUpdateAtScript = nullptr;
    InvokeStartAtScript invokeStartAtScript = nullptr;
    InvokeBeginCollisionTouch invokeBeginCollisionTouch = nullptr;
    InvokeEndCollisionTouch invokeEndCollisionTouch = nullptr;
    InvokeBeginSensorOverlap invokeBeginSensorOverlap = nullptr;
    InvokeEndSensorOverlap invokeEndSensorOverlap = nullptr;
    EntityWillBeDeleted entityWillBeDeleted = nullptr;
    GetManifest getManifest = nullptr;
    UIUpdate ui_Update = nullptr;
    UIHandleEvent ui_HandleEvent = nullptr;
    UIWindowClosed ui_WindowClosed = nullptr;
    AsyncUpdate async_Update = nullptr;
    AsyncCancelAll async_CancelAll = nullptr;
    AsyncWaitAll async_WaitAll = nullptr;
} // namespace ExternalCalls

namespace {

    bool s_hostApiInitialized = false;
    ScriptHostApi s_hostApi;

    void initScriptHostApi() {
        if (s_hostApiInitialized) { return; }
        s_hostApi.abiVersion = Scripting::SCRIPT_ABI_VERSION;
        s_hostApi.application_Quit = InternalCalls::application_Quit;
        s_hostApi.application_GetWidth = InternalCalls::application_GetWidth;
        s_hostApi.application_GetHeight = InternalCalls::application_GetHeight;
        s_hostApi.application_GetTime = InternalCalls::application_GetTime;
        s_hostApi.application_IsCursorLocked = InternalCalls::application_IsCursorLocked;
        s_hostApi.application_SetCursorLocked = InternalCalls::application_SetCursorLocked;
        s_hostApi.application_GetPersistentDataPath = InternalCalls::application_GetPersistentDataPath;
        s_hostApi.world_Load = InternalCalls::world_Load;
        s_hostApi.world_FindByTag = InternalCalls::world_FindByTag;
        s_hostApi.world_FindAllByTag = InternalCalls::world_FindAllByTag;
        s_hostApi.world_CreateEntity = InternalCalls::world_CreateEntity;
        s_hostApi.world_DestroyEntity = InternalCalls::world_DestroyEntity;
        s_hostApi.worldLighting_SetAmbientColor = InternalCalls::worldLighting_SetAmbientColor;
        s_hostApi.worldLighting_SetAmbientIntensity = InternalCalls::worldLighting_SetAmbientIntensity;
        s_hostApi.input_IsKeyPressed = InternalCalls::input_IsKeyPressed;
        s_hostApi.input_IsKeyJustPressed = InternalCalls::input_IsKeyJustPressed;
        s_hostApi.input_IsMouseButtonPressed = InternalCalls::input_IsMouseButtonPressed;
        s_hostApi.input_IsMouseButtonJustPressed = InternalCalls::input_IsMouseButtonJustPressed;
        s_hostApi.input_GetMouseDeltaX = InternalCalls::input_GetMouseDeltaX;
        s_hostApi.input_GetMouseDeltaY = InternalCalls::input_GetMouseDeltaY;
        s_hostApi.input_GetTouchCount = InternalCalls::input_GetTouchCount;
        s_hostApi.input_GetTouch = InternalCalls::input_GetTouch;
        s_hostApi.entity_CreateComponent = InternalCalls::entity_CreateComponent;
        s_hostApi.entity_HasComponent = InternalCalls::entity_HasComponent;
        s_hostApi.entity_RemoveComponent = InternalCalls::entity_RemoveComponent;
        s_hostApi.entity_GetName = InternalCalls::entity_GetName;
        s_hostApi.transformComponent_GetPosition = InternalCalls::transformComponent_GetPosition;
        s_hostApi.transformComponent_SetPosition = InternalCalls::transformComponent_SetPosition;
        s_hostApi.transformComponent_Translate = InternalCalls::transformComponent_Translate;
        s_hostApi.transformComponent_GetRotationEuler = InternalCalls::transformComponent_GetRotationEuler;
        s_hostApi.transformComponent_SetRotationEuler = InternalCalls::transformComponent_SetRotationEuler;
        s_hostApi.transformComponent_GetRotation = InternalCalls::transformComponent_GetRotation;
        s_hostApi.transformComponent_SetRotation = InternalCalls::transformComponent_SetRotation;
        s_hostApi.transformComponent_RotateEuler = InternalCalls::transformComponent_RotateEuler;
        s_hostApi.transformComponent_GetScale = InternalCalls::transformComponent_GetScale;
        s_hostApi.transformComponent_SetScale = InternalCalls::transformComponent_SetScale;
        s_hostApi.spriteRendererComponent_GetColor = InternalCalls::spriteRendererComponent_GetColor;
        s_hostApi.spriteRendererComponent_SetColor = InternalCalls::spriteRendererComponent_SetColor;
        s_hostApi.spriteRendererComponent_GetMaterial = InternalCalls::spriteRendererComponent_GetMaterial;
        s_hostApi.spriteRendererComponent_SetMaterial = InternalCalls::spriteRendererComponent_SetMaterial;
        s_hostApi.spriteRendererComponent_GetTexture = InternalCalls::spriteRendererComponent_GetTexture;
        s_hostApi.spriteRendererComponent_SetTexture = InternalCalls::spriteRendererComponent_SetTexture;
        s_hostApi.spriteRendererComponent_SetCell = InternalCalls::spriteRendererComponent_SetCell;
        s_hostApi.meshComponent_GetMesh = InternalCalls::meshComponent_GetMesh;
        s_hostApi.meshComponent_SetMesh = InternalCalls::meshComponent_SetMesh;
        s_hostApi.meshComponent_SetMaterial = InternalCalls::meshComponent_SetMaterial;
        s_hostApi.directionalLightComponent_SetColor = InternalCalls::directionalLightComponent_SetColor;
        s_hostApi.directionalLightComponent_SetIntensity = InternalCalls::directionalLightComponent_SetIntensity;
        s_hostApi.directionalLightComponent_SetEnabled = InternalCalls::directionalLightComponent_SetEnabled;
        s_hostApi.cameraComponent_SetPostprocessingEnabled = InternalCalls::cameraComponent_SetPostprocessingEnabled;
        s_hostApi.cameraComponent_GetPostprocessingEnabled = InternalCalls::cameraComponent_GetPostprocessingEnabled;
        s_hostApi.cameraComponent_SetBloom = InternalCalls::cameraComponent_SetBloom;
        s_hostApi.cameraComponent_GetBloom = InternalCalls::cameraComponent_GetBloom;
        s_hostApi.material_SetFloatValue = InternalCalls::material_SetFloatValue;
        s_hostApi.material_SetVec4Value = InternalCalls::material_SetVec4Value;
        s_hostApi.material_SetTextureValue = InternalCalls::material_SetTextureValue;
        s_hostApi.texture_ReadPixelsRGBA8 = InternalCalls::texture_ReadPixelsRGBA8;
        s_hostApi.mesh_Create = InternalCalls::mesh_Create;
        s_hostApi.mesh_Update = InternalCalls::mesh_Update;
        s_hostApi.mesh_Delete = InternalCalls::mesh_Delete;
        s_hostApi.texture_CreateRGBA8 = InternalCalls::texture_CreateRGBA8;
        s_hostApi.texture_UpdateRGBA8 = InternalCalls::texture_UpdateRGBA8;
        s_hostApi.texture_Delete = InternalCalls::texture_Delete;
        s_hostApi.hostApiTable_Find = InternalCalls::hostApiTable_Find;
        s_hostApi.font_ReadData = InternalCalls::font_ReadData;
        s_hostApi.console_Log = InternalCalls::console_Log;
        PandaUIImpl::bindHostApi(s_hostApi);

        s_hostApiInitialized = true;
    }

} // namespace

void bindScriptPluginApi(const ScriptPluginApi *api) {
    if (!api || api->abiVersion != Scripting::SCRIPT_ABI_VERSION) {
        clearScriptPluginApi();
        return;
    }
    using namespace ExternalCalls;
    clear = api->clear;
    instantiateScript = api->instantiateScript;
    setFieldValue = api->setFieldValue;
    invokeUpdateAtScript = api->invokeUpdateAtScript;
    invokeFixedUpdateAtScript = api->invokeFixedUpdateAtScript;
    invokeLateUpdateAtScript = api->invokeLateUpdateAtScript;
    invokeStartAtScript = api->invokeStartAtScript;
    invokeBeginCollisionTouch = api->invokeBeginCollisionTouch;
    invokeEndCollisionTouch = api->invokeEndCollisionTouch;
    invokeBeginSensorOverlap = api->invokeBeginSensorOverlap;
    invokeEndSensorOverlap = api->invokeEndSensorOverlap;
    entityWillBeDeleted = api->entityWillBeDeleted;
    getManifest = api->getManifest;
    ui_Update = api->ui_Update;
    ui_HandleEvent = api->ui_HandleEvent;
    ui_WindowClosed = api->ui_WindowClosed;
    async_Update = api->async_Update;
    async_CancelAll = api->async_CancelAll;
    async_WaitAll = api->async_WaitAll;
    LOG_INFO("SCRIPT ENGINE: Inner functions binding done.");
}

void clearScriptPluginApi() {
    using namespace ExternalCalls;
    clear = nullptr;
    instantiateScript = nullptr;
    setFieldValue = nullptr;
    invokeUpdateAtScript = nullptr;
    invokeFixedUpdateAtScript = nullptr;
    invokeLateUpdateAtScript = nullptr;
    invokeStartAtScript = nullptr;
    invokeBeginCollisionTouch = nullptr;
    invokeEndCollisionTouch = nullptr;
    invokeBeginSensorOverlap = nullptr;
    invokeEndSensorOverlap = nullptr;
    entityWillBeDeleted = nullptr;
    getManifest = nullptr;
    ui_Update = nullptr;
    ui_HandleEvent = nullptr;
    ui_WindowClosed = nullptr;
    async_Update = nullptr;
    async_CancelAll = nullptr;
    async_WaitAll = nullptr;
}

void updateScriptAsync(uint32_t maxCompletions, double maxMilliseconds) {
    if (ExternalCalls::async_Update) { ExternalCalls::async_Update(maxCompletions, maxMilliseconds); }
}

void cancelAndWaitScriptAsync() {
    if (ExternalCalls::async_CancelAll) { ExternalCalls::async_CancelAll(); }
    if (ExternalCalls::async_WaitAll) { ExternalCalls::async_WaitAll(); }
}

const ScriptHostApi *getScriptHostApi() {
    initScriptHostApi();
    return &s_hostApi;
}

void freeScriptingResources() {
    // A dedicated allocator could be created for scripting so that the memory allocation is on
    // the engine side. On a reload of the dynamic library all the memory allocated by the
    // scripts could then be freed.
    s_scriptCreatedAssets.clear();
}

} // namespace Panda
