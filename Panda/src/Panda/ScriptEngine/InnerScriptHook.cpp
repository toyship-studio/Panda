#include "Panda/ScriptEngine/InnerScriptHook.hpp"
#include "Panda/UI/GameUI.hpp"
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
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace Panda {

std::unordered_map<UUID, AssetRef<Asset>> s_scriptCreatedAssets;

// The Handle comes from a user script: a broken reference (the entity was deleted at runtime,
// the field is not set, the id is from another world) must not crash the engine. An invalid
// Entity is returned + a one-time error in the console for each id — the call becomes a no-op.
// The entity is looked up in the running world: scripts exist and act only there.
Entity resolveScriptEntity(Handle entityHandle) {
    World *world = GameContext::getRunningWorld();
    Entity entity = world ? world->getById(entityHandle) : Entity();
    if (!entity.isValid()) {
        static std::unordered_set<uint32_t> reported;
        if (reported.insert((uint32_t)entityHandle).second) {
            LOG_ERROR("A script called the engine with an entity that does not exist (%u).", (uint32_t)entityHandle);
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

    // The size of the view of the game, in the points the pointer is given in: the Game tab in
    // the editor, the window in a game.
    uint32_t application_GetWidth() {
        const Rect frame = Input::getViewportFrame();
        if (frame.size.width > 0.f) { return static_cast<uint32_t>(frame.size.width); }
        return Panda::Application::get()->getMainWindow()->getSize().width;
    }

    uint32_t application_GetHeight() {
        const Rect frame = Input::getViewportFrame();
        if (frame.size.height > 0.f) { return static_cast<uint32_t>(frame.size.height); }
        return Panda::Application::get()->getMainWindow()->getSize().height;
    }

    float application_GetTime() {
        return Panda::Application::get()->getTime();
    }

    bool application_IsCursorLocked() {
        return Input::isCursorVirtual() ? Input::isVirtualCursorLocked() : Fern::isCursorLocked();
    }

    void application_SetCursorLocked(bool locked) {
        if (locked && !Input::isFocused()) { return; } // the game has no input focus: lock it again in onApplicationFocus(true)
        // A driven game (Input::setCursorVirtual) leaves the real cursor to the user.
        if (Input::isCursorVirtual()) {
            Input::setVirtualCursorLocked(locked);
            return;
        }
        Fern::setCursorLocked(locked);
    }

    uint32_t application_GetPersistentDataPath(void *destination, uint32_t destinationSize) {
        // The folder appears when a game asks where it is: a game that keeps nothing leaves
        // nothing on the disk of the player.
        const path_t &directory = GameContext::getPersistentDataPath();
        if (!directory.empty()) {
            std::error_code error;
            std::filesystem::create_directories(directory, error);
        }
        const std::string path = directory.string();
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

    void input_GetMousePosition(float *x, float *y) {
        if (x) { *x = static_cast<float>(Input::getMouseViewportPositionX()); }
        if (y) { *y = static_cast<float>(Input::getMouseViewportPositionY()); }
    }

    void input_GetMouseScroll(float *x, float *y) {
        if (x) { *x = static_cast<float>(Input::getMouseScrollX()); }
        if (y) { *y = static_cast<float>(Input::getMouseScrollY()); }
    }

    bool input_GetTouch(int index, int *id, float *x, float *y) {
        if (index < 0 || index >= Input::touchCount()) { return false; }

        // In the view of the game, as the mouse: the Game tab in the editor, the window in a game.
        Input::Touch touch = Input::getTouch(index);
        const Rect frame = Input::getViewportFrame();
        if (id) { *id = touch.id; }
        if (x) { *x = touch.x - frame.origin.x; }
        if (y) { *y = touch.y - frame.origin.y; }
        return true;
    }

    /// WORLD

    void world_Load(Handle worldHandle) {
        GameContext::requestWorldLoad(AssetId(worldHandle));
    }

    // The world functions act in the running world. A call that arrives while no world runs (a
    // UI callback of the bundle after the stop) finds nothing and creates nothing.
    Handle world_FindByTag(const char *tag) {
        World *world = GameContext::getRunningWorld();
        if (!world) { return 0; }
        Entity entity = world->findByTag(tag);
        return entity.isValid() ? (Handle)entity.getId() : 0; // not found — 0, not a getId assert
    }

    uint32_t world_FindAllByTag(const char *tag, Handle *entityHandles, uint32_t capacity) {
        World *world = GameContext::getRunningWorld();
        if (!world) { return 0; }
        std::vector<Entity> entities = world->findAllByTag(tag);
        const uint32_t count = static_cast<uint32_t>(entities.size());
        if (!entityHandles || capacity == 0) { return count; }

        const uint32_t writeCount = std::min(count, capacity);
        for (uint32_t i = 0; i < writeCount; ++i) {
            entityHandles[i] = entities[i].getId();
        }
        return count;
    }

    Handle world_CreateEntity(const char *tag) {
        World *world = GameContext::getRunningWorld();
        if (!world) { return 0; }
        Entity entity = world->instantiateEntity();
        entity.setName(tag);
        return entity.getId();
    }

    void world_DestroyEntity(Handle id) {
        Entity entity = resolveScriptEntity(id);
        if (!entity.isValid()) { return; }
        entity.getWorld()->destroy(entity); // a running world: after the callbacks
    }

    void worldLighting_SetAmbientColor(float r, float g, float b, float a) {
        World *world = GameContext::getRunningWorld();
        if (!world) { return; }
        world->getLightingSettings().ambientColor = Color(r, g, b, a);
    }

    void worldLighting_SetAmbientIntensity(float intensity) {
        World *world = GameContext::getRunningWorld();
        if (!world) { return; }
        world->getLightingSettings().ambientIntensity = intensity;
    }

    void worldLighting_SetEnvironmentIntensity(float intensity) {
        World *world = GameContext::getRunningWorld();
        if (!world) { return; }
        world->getLightingSettings().environmentIntensity = intensity;
    }

    void worldLighting_SetEnvironmentMap(Handle textureId) {
        if (textureId && !AssetRef<TextureAsset>(textureId).get()) {
            LOG_ERROR_EDITOR("A script sets texture %u as the environment map, and it does not exist.", static_cast<unsigned>(textureId));
            return;
        }
        World *world = GameContext::getRunningWorld();
        if (!world) { return; }
        world->getLightingSettings().environmentMap = AssetRef<Asset>(textureId);
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
                LOG_ERROR("A script asked for a component type the engine does not know: %s.", type);
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
                LOG_ERROR_EDITOR("A script refers to texture %u, which does not exist.", textureRef.getId());
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
            LOG_ERROR_EDITOR("A script refers to material %u, which does not exist.", materialRef.getId());
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

    /// POINT LIGHT COMPONENT

    // A lamp of a script: the component is made by the first call.
    void pointLightComponent_Set(Handle entityHandle, float r, float g, float b, float intensity, float radius, bool enabled) {
        Entity entity = resolveScriptEntity(entityHandle);
        if (!entity.isValid()) { return; }
        PointLightComponent &light =
            entity.hasComponent<PointLightComponent>() ? entity.getComponent<PointLightComponent>() : entity.addComponent<PointLightComponent>();
        light.color = Color(r, g, b, 1.f);
        light.intensity = intensity;
        light.radius = radius;
        light.isEnabled = enabled;
    }

    /// CAMERA COMPONENT

    void cameraComponent_SetFieldOfView(Handle entityHandle, float degrees) {
        Entity entity = resolveScriptEntity(entityHandle);
        if (!entity.isValid() || !entity.hasComponent<CameraComponent>()) { return; }
        entity.getComponent<CameraComponent>().camera.setFieldOfView(degrees);
    }

    float cameraComponent_GetFieldOfView(Handle entityHandle) {
        Entity entity = resolveScriptEntity(entityHandle);
        if (!entity.isValid() || !entity.hasComponent<CameraComponent>()) { return 0.f; }
        return entity.getComponent<CameraComponent>().camera.getFieldOfView();
    }

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
        // An id of the runtime range: it cannot fall on an asset of the project.
        auto meshRef = GameContext::getAssetHandler()->createRuntimeAsset<RuntimeMeshAsset>();
        *handle = meshRef.getId();
        s_scriptCreatedAssets[meshRef.getId()] = meshRef.asBaseAsset();
    }

    void mesh_Update(
        Handle handle, const void *vertices, uint32_t verticesCount, const void *indices, uint32_t indicesCount
    ) {
        auto meshRef = AssetRef<RuntimeMeshAsset>(handle);
        if (!meshRef) {
            LOG_ERROR_EDITOR("A script refers to mesh %u, which does not exist.", meshRef.getId());
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

    // The model is read from its file once for the two calls of the pattern (the counts, then the
    // buffers) and let go after the fill.
    bool mesh_Read(
        Handle meshHandle, void *vertices, uint32_t vertexCapacity, uint32_t *vertexCount, uint32_t *indices,
        uint32_t indexCapacity, uint32_t *indexCount
    ) {
        static Handle s_readHandle = 0;
        static std::vector<Vertex> s_readVertices;
        static std::vector<uint32_t> s_readIndices;
        if (vertexCount) { *vertexCount = 0; }
        if (indexCount) { *indexCount = 0; }
        AssetHandler *assetHandler = GameContext::getAssetHandler();
        if (!assetHandler || !meshHandle) { return false; }
        if (s_readHandle != meshHandle) {
            s_readHandle = 0;
            s_readVertices.clear();
            s_readIndices.clear();
            std::vector<ImportedMeshData> meshes;
            if (!assetHandler->readMeshData(AssetId(meshHandle), meshes)) {
                LOG_ERROR_EDITOR("A script reads mesh %u, which is not a mesh asset of the project.", static_cast<unsigned>(meshHandle));
                return false;
            }
            for (const ImportedMeshData &mesh : meshes) {
                for (const ImportedPrimitiveData &primitive : mesh.primitives) {
                    const uint32_t base = static_cast<uint32_t>(s_readVertices.size());
                    s_readVertices.insert(s_readVertices.end(), primitive.vertices.begin(), primitive.vertices.end());
                    for (uint32_t index : primitive.indices) { s_readIndices.push_back(base + index); }
                }
            }
            s_readHandle = meshHandle;
        }
        if (vertexCount) { *vertexCount = static_cast<uint32_t>(s_readVertices.size()); }
        if (indexCount) { *indexCount = static_cast<uint32_t>(s_readIndices.size()); }
        if (vertices && indices && vertexCapacity >= s_readVertices.size() && indexCapacity >= s_readIndices.size()) {
            std::memcpy(vertices, s_readVertices.data(), s_readVertices.size() * sizeof(Vertex));
            std::memcpy(indices, s_readIndices.data(), s_readIndices.size() * sizeof(uint32_t));
            s_readHandle = 0;
            s_readVertices = {};
            s_readIndices = {};
        }
        return true;
    }

    void mesh_Delete(Handle handle) {
        if (!s_scriptCreatedAssets.contains(handle)) {
            LOG_ERROR_EDITOR("Mesh %u was not created by a script: a script deletes only the meshes it created.", handle);
            return;
        }
        s_scriptCreatedAssets.erase(handle);
    }

    /// RUNTIME TEXTURE (a mirror of the runtime meshes: dynamic textures of games)

    void texture_CreateRGBA8(uint32_t width, uint32_t height, Handle *handle) {
        AssetHandler *assetHandler = GameContext::getAssetHandler();
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
        auto textureRef = assetHandler->createRuntimeAsset<TextureAsset>(create);
        *handle = textureRef.getId();
        s_scriptCreatedAssets[textureRef.getId()] = textureRef.asBaseAsset();
    }

    void texture_UpdateRGBA8(Handle handle, const void *pixels, uint32_t sizeBytes) {
        auto textureRef = AssetRef<TextureAsset>(handle);
        if (!textureRef) {
            LOG_ERROR_EDITOR("A script refers to texture %u, which does not exist.", handle);
            return;
        }
        const Size textureSize = textureRef->getSize();
        const uint32_t expectedBytes =
            static_cast<uint32_t>(textureSize.width) * static_cast<uint32_t>(textureSize.height) * 4;
        if (sizeBytes != expectedBytes) {
            LOG_ERROR_EDITOR("Texture %u: the update has %u bytes, the texture takes %u.", handle, sizeBytes, expectedBytes);
            return;
        }
        Miren::updateTexture(textureRef->getMirenHandle(), Foundation::Memory::copying(pixels, sizeBytes));
    }

    void texture_Delete(Handle handle) {
        if (!s_scriptCreatedAssets.contains(handle)) {
            LOG_ERROR_EDITOR("Texture %u was not created by a script: a script updates only the textures it created.", handle);
            return;
        }
        s_scriptCreatedAssets.erase(handle);
    }

    /// HOST API TABLES — named tables of contributions, see ScriptHostApis.hpp.

    const void *hostApiTable_Find(const char *name, uint32_t version) {
        const ScriptHostApiRegistry &registry = GameContext::getExtensions().scriptHostApis;
        const std::string_view wanted = name != nullptr ? name : "";
        const void *table = registry.find(wanted, version);
        if (table != nullptr) { return table; }
        // The table is there in another version: the scripts were built with another SDK than the
        // engine has, and every call of that API would do nothing in silence. Said once per bundle
        // (the SDK side asks again and again while it has no table).
        static std::unordered_set<std::string> s_reported;
        for (const ScriptHostApiTable &other : registry.tables()) {
            if (other.name != wanted) { continue; }
            const std::string key = other.name + ':' + std::to_string(version);
            if (s_reported.insert(key).second) {
                LOG_ERROR_EDITOR(
                    "The scripts were built for the \"%s\" API of version %u, the engine has version %u: its calls do "
                    "nothing until the scripts are built again.",
                    other.name.c_str(), version, other.version
                );
            }
        }
        return nullptr;
    }

    /// CONSOLE

    // A line a script logs is for the user: in the editor it goes to the Output panel, and the
    // log of the editor writes it to the console as well — one call, or the console has it twice.
    void console_Log(int type, const char *message) {
        const char *safeMessage = message ? message : "";
        const bool editor = Foundation::EditorLogger::isInitialized();
        if (type == 0) {
            if (editor) {
                LOG_INFO_EDITOR("%s", safeMessage);
            } else {
                LOG_INFO("%s", safeMessage);
            }
        } else if (type == 1) {
            if (editor) {
                LOG_WARN_EDITOR("%s", safeMessage);
            } else {
                LOG_WARN("%s", safeMessage);
            }
        } else if (type == 2) {
            if (editor) {
                LOG_ERROR_EDITOR("%s", safeMessage);
            } else {
                LOG_ERROR("%s", safeMessage);
            }
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
    InvokeApplicationFocusAtScript invokeApplicationFocusAtScript = nullptr;
    InvokeApplicationPauseAtScript invokeApplicationPauseAtScript = nullptr;
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
        s_hostApi.input_GetMousePosition = InternalCalls::input_GetMousePosition;
        s_hostApi.input_GetMouseScroll = InternalCalls::input_GetMouseScroll;
        s_hostApi.pointLightComponent_Set = InternalCalls::pointLightComponent_Set;
        s_hostApi.worldLighting_SetEnvironmentIntensity = InternalCalls::worldLighting_SetEnvironmentIntensity;
        s_hostApi.cameraComponent_SetFieldOfView = InternalCalls::cameraComponent_SetFieldOfView;
        s_hostApi.cameraComponent_GetFieldOfView = InternalCalls::cameraComponent_GetFieldOfView;
        s_hostApi.worldLighting_SetEnvironmentMap = InternalCalls::worldLighting_SetEnvironmentMap;
        s_hostApi.mesh_Read = InternalCalls::mesh_Read;
        s_hostApi.console_Log = InternalCalls::console_Log;
        GameUI::bindHostApi(s_hostApi);

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
    invokeApplicationFocusAtScript = api->invokeApplicationFocusAtScript;
    invokeApplicationPauseAtScript = api->invokeApplicationPauseAtScript;
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
    invokeApplicationFocusAtScript = nullptr;
    invokeApplicationPauseAtScript = nullptr;
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
