#include "Panda/GameLogic/Components/CoreComponentTypes.hpp"
#include "Panda/GameLogic/Entity.hpp"
#include "Panda/GameLogic/World.hpp" // the live children of an entity (encodeRelationship)
#include "Panda/Serialization/ComponentDtos.hpp"

namespace Panda {

namespace {

    // --- Mandatory entity components: instantiateEntity creates them by default,
    // --- decode updates them in place.

    void encodeTag(Rain::Encoder &encoder, Entity entity) {
        encoder.encode("tagComponent", entity.getComponent<TagComponent>());
    }
    void decodeTag(Rain::Decoder &decoder, Entity entity) {
        TagComponent tag;
        if (decoder.decode("tagComponent", tag)) { entity.setName(tag.tag); }
    }

    // The file gets the live children only. A child deleted by an undoable command is a
    // tombstone: it stays in the registry and in this list for the undo, but it is not written,
    // and a file that listed it crashed the next load (World::repairHierarchy mends such files).
    void encodeRelationship(Rain::Encoder &encoder, Entity entity) {
        const RelationshipComponent &relationship = entity.getComponent<RelationshipComponent>();
        World &world = *entity.getWorld();
        RelationshipComponentDto dto;
        dto.parent = relationship.getParent();
        for (UUID child : relationship.getChildren()) {
            if (world.isValid(child)) { dto.children.push_back(child); }
        }
        encoder.encode("relationshipComponent", dto);
    }
    // The component carries the same keys as the DTO: it is read as it is.
    void decodeRelationship(Rain::Decoder &decoder, Entity entity) {
        RelationshipComponent relationship;
        if (decoder.decode("relationshipComponent", relationship)) {
            entity.setComponent<RelationshipComponent>(relationship);
        }
    }

    void encodeTransform(Rain::Encoder &encoder, Entity entity) {
        TransformComponent &transform = entity.getTransform();
        TransformComponentDto dto;
        dto.position = transform.getPosition();
        dto.rotation = transform.getRotation();
        dto.scale = transform.getScale();
        encoder.encode("transformComponent", dto);
    }
    void decodeTransform(Rain::Decoder &decoder, Entity entity) {
        TransformComponentDto dto;
        if (!decoder.decode("transformComponent", dto)) { return; }
        TransformComponent &transform = entity.getTransform();
        transform.setPosition(dto.position);
        transform.setRotation(dto.rotation);
        transform.setScale(dto.scale);
    }

    void encodeScripts(Rain::Encoder &encoder, Entity entity) {
        ScriptListComponentDto dto;
        for (ExternalScript &script : entity.getComponent<ScriptListComponent>().scripts) {
            std::vector<ScriptFieldDto> fields;
            for (ScriptField &field : script.getFields()) {
                ScriptFieldDto fieldDto;
                fieldDto.name = field.name;
                fieldDto.fieldId = field.fieldId;
                fieldDto.type = field.type;
                fieldDto.value = field.value;
                fields.emplace_back(fieldDto);
            }
            dto.scripts.emplace_back(script.getName(), fields);
        }
        encoder.encode("scriptListComponent", dto);
    }
    void decodeScripts(Rain::Decoder &decoder, Entity entity) {
        ScriptListComponentDto dto;
        if (!decoder.decode("scriptListComponent", dto)) { return; }
        // The component says what the scripts of the entity are: read into an entity that has
        // some — an instance of a prefab brought to its entry (Prefabs::setEntry), a component
        // set by its text — it replaces them, it does not join them.
        entity.getComponent<ScriptListComponent>().scripts.clear();
        for (ExternalScriptDto &scriptDto : dto.scripts) {
            std::vector<ScriptField> fields;
            for (ScriptFieldDto &fieldDto : scriptDto.scriptFields) {
                fields.emplace_back(0, fieldDto.fieldId, fieldDto.name, fieldDto.type, fieldDto.value);
            }
            entity.addScript(ExternalScript(entity.getId(), 0, scriptDto.name, fields));
        }
    }

    // The fields of the type entity are the references of the scripts to entities.
    void scriptEntityRefs(Entity entity, const EntityRefVisitor &visit) {
        for (ExternalScript &script : entity.getComponent<ScriptListComponent>().scripts) {
            for (ScriptField &field : script.getFields()) {
                if (field.type != ScriptFieldType::ENTITY) { continue; }
                if (UUID *one = std::get_if<UUID>(&field.value)) {
                    visit(*one);
                } else if (std::vector<UUID> *many = std::get_if<std::vector<UUID>>(&field.value)) {
                    for (UUID &id : *many) { visit(id); }
                }
            }
        }
    }

    // --- The link to a prefab: where an entity of an instance came from (context/prefabs.md).

    void encodePrefab(Rain::Encoder &encoder, Entity entity) {
        const PrefabComponent &prefab = entity.getComponent<PrefabComponent>();
        PrefabComponentDto dto;
        dto.instance = prefab.instance;
        dto.source = prefab.source;
        dto.prefab = prefab.prefab;
        encoder.encode(PREFAB_LINK_KEY, dto);
    }
    void decodePrefab(Rain::Decoder &decoder, Entity entity) {
        PrefabComponentDto dto;
        if (!decoder.decode(PREFAB_LINK_KEY, dto)) { return; }
        PrefabComponent &prefab = entity.addComponent<PrefabComponent>();
        prefab.instance = dto.instance;
        prefab.source = dto.source;
        prefab.prefab = dto.prefab;
    }
    void prefabEntityRefs(Entity entity, const EntityRefVisitor &visit) {
        visit(entity.getComponent<PrefabComponent>().instance);
    }

    // --- Static tag: a bool in the file (in old worlds every entity has false).

    void encodeStatic(Rain::Encoder &encoder, Entity) {
        encoder.encode("isStatic", true);
    }
    void decodeStatic(Rain::Decoder &decoder, Entity entity) {
        bool isStatic = false;
        if (decoder.decode("isStatic", isStatic) && isStatic) { entity.addTag<IsStaticComponent>(); }
    }

    // --- Optional components: decode adds them.

    void encodeCamera(Rain::Encoder &encoder, Entity entity) {
        CameraComponent &camera = entity.getComponent<CameraComponent>();
        CameraComponentDto dto;
        dto.isMain = camera.isMain;
        dto.camera.projectionType = camera.camera.getProjectionType();
        dto.camera.zNear = camera.camera.getNear();
        dto.camera.zFar = camera.camera.getFar();
        dto.camera.fieldOfView = camera.camera.getFieldOfView();
        dto.camera.orthoSize = camera.camera.getOrthoSize();
        dto.postprocessing = camera.postprocessing.getId();
        dto.postprocessingEnabled = camera.postprocessingEnabled;
        dto.bloomEnabled = camera.bloomEnabled;
        dto.bloomIntensity = camera.bloomIntensity;
        dto.bloomRadius = camera.bloomRadius;
        dto.autoExposureEnabled = camera.autoExposureEnabled;
        dto.autoExposureKey = camera.autoExposureKey;
        dto.autoExposureSpeed = camera.autoExposureSpeed;
        dto.autoExposureMin = camera.autoExposureMin;
        dto.autoExposureMax = camera.autoExposureMax;
        dto.tonemap = tonemapName(camera.tonemap);
        dto.exposure = camera.exposure;
        dto.antialiasing = camera.antialiasing;
        dto.ambientOcclusion = camera.ambientOcclusion;
        dto.ambientOcclusionRadius = camera.ambientOcclusionRadius;
        dto.ambientOcclusionIntensity = camera.ambientOcclusionIntensity;
        encoder.encode("cameraComponent", dto);
    }
    void decodeCamera(Rain::Decoder &decoder, Entity entity) {
        CameraComponentDto dto;
        if (!decoder.decode("cameraComponent", dto)) { return; }
        CameraComponent &camera = entity.addComponent<CameraComponent>();
        camera.isMain = dto.isMain;
        camera.camera.setProjectionType(dto.camera.projectionType);
        camera.camera.setNear(dto.camera.zNear);
        camera.camera.setFar(dto.camera.zFar);
        camera.camera.setFieldOfView(dto.camera.fieldOfView);
        camera.camera.setOrthoSize(dto.camera.orthoSize);
        if (dto.postprocessing) { camera.postprocessing = AssetRef<Asset>(dto.postprocessing); }
        camera.postprocessingEnabled = dto.postprocessingEnabled;
        camera.bloomEnabled = dto.bloomEnabled;
        camera.bloomIntensity = dto.bloomIntensity;
        camera.bloomRadius = dto.bloomRadius;
        camera.autoExposureEnabled = dto.autoExposureEnabled;
        camera.autoExposureKey = dto.autoExposureKey;
        camera.autoExposureSpeed = dto.autoExposureSpeed;
        camera.autoExposureMin = dto.autoExposureMin;
        camera.autoExposureMax = dto.autoExposureMax;
        camera.tonemap = tonemapFromName(dto.tonemap);
        camera.exposure = dto.exposure;
        camera.antialiasing = dto.antialiasing;
        camera.ambientOcclusion = dto.ambientOcclusion;
        camera.ambientOcclusionRadius = dto.ambientOcclusionRadius;
        camera.ambientOcclusionIntensity = dto.ambientOcclusionIntensity;
    }

    void encodeDirectionalLight(Rain::Encoder &encoder, Entity entity) {
        DirectionalLightComponent &light = entity.getComponent<DirectionalLightComponent>();
        DirectionalLightComponentDto dto;
        dto.isMain = light.isMain;
        dto.color = light.color;
        dto.intensity = light.intensity;
        dto.isEnabled = light.isEnabled;
        dto.castShadows = light.castShadows;
        dto.shadowStrength = light.shadowStrength;
        dto.shadowSoftness = light.shadowSoftness;
        encoder.encode("directionalLightComponent", dto);
    }
    void decodeDirectionalLight(Rain::Decoder &decoder, Entity entity) {
        DirectionalLightComponentDto dto;
        if (!decoder.decode("directionalLightComponent", dto)) { return; }
        DirectionalLightComponent &light = entity.addComponent<DirectionalLightComponent>();
        light.isMain = dto.isMain;
        light.color = dto.color;
        light.intensity = dto.intensity;
        light.isEnabled = dto.isEnabled;
        light.castShadows = dto.castShadows;
        light.shadowStrength = dto.shadowStrength;
        light.shadowSoftness = dto.shadowSoftness;
    }

    void encodePointLight(Rain::Encoder &encoder, Entity entity) {
        PointLightComponent &light = entity.getComponent<PointLightComponent>();
        PointLightComponentDto dto;
        dto.color = light.color;
        dto.intensity = light.intensity;
        dto.radius = light.radius;
        dto.isEnabled = light.isEnabled;
        dto.coneAngle = light.coneAngle;
        dto.coneSoftness = light.coneSoftness;
        dto.castShadows = light.castShadows;
        encoder.encode("pointLightComponent", dto);
    }
    void decodePointLight(Rain::Decoder &decoder, Entity entity) {
        PointLightComponentDto dto;
        if (!decoder.decode("pointLightComponent", dto)) { return; }
        PointLightComponent &light = entity.addComponent<PointLightComponent>();
        light.color = dto.color;
        light.intensity = dto.intensity;
        light.radius = dto.radius;
        light.isEnabled = dto.isEnabled;
        light.coneAngle = dto.coneAngle;
        light.coneSoftness = dto.coneSoftness;
        light.castShadows = dto.castShadows;
    }

    void encodeSprite(Rain::Encoder &encoder, Entity entity) {
        SpriteRendererComponent &sprite = entity.getComponent<SpriteRendererComponent>();
        SpriteRendererComponentDto dto;
        dto.color = sprite.color;
        dto.cols = sprite.cols;
        dto.rows = sprite.rows;
        dto.index = sprite.index;
        dto.material = sprite.material.getId();
        dto.texture = sprite.texture.getId();
        encoder.encode("spriteRendererComponent", dto);
    }
    void decodeSprite(Rain::Decoder &decoder, Entity entity) {
        SpriteRendererComponentDto dto;
        if (!decoder.decode("spriteRendererComponent", dto)) { return; }
        SpriteRendererComponent &sprite = entity.addComponent<SpriteRendererComponent>();
        sprite.color = dto.color;
        sprite.cols = dto.cols;
        sprite.rows = dto.rows;
        sprite.index = dto.index;
        sprite.material = AssetRef<Asset>(dto.material);
        sprite.texture = AssetRef<Asset>(dto.texture);
    }

    void encodeImportedSubmesh(Rain::Encoder &encoder, Entity entity) {
        ImportedSubmeshComponent &submesh = entity.getComponent<ImportedSubmeshComponent>();
        ImportedSubmeshComponentDto dto;
        dto.importedMesh = submesh.importedMesh.getId();
        dto.overrideMaterial = submesh.material.getId();
        dto.submeshIndex = submesh.submeshIndex;
        dto.primitiveIndex = submesh.primitiveIndex;
        encoder.encode("importedSubmeshComponent", dto);
    }
    void decodeImportedSubmesh(Rain::Decoder &decoder, Entity entity) {
        ImportedSubmeshComponentDto dto;
        if (!decoder.decode("importedSubmeshComponent", dto)) { return; }
        ImportedSubmeshComponent &submesh = entity.addComponent<ImportedSubmeshComponent>();
        submesh.importedMesh = AssetRef<Asset>(dto.importedMesh);
        submesh.material = AssetRef<Asset>(dto.overrideMaterial);
        submesh.submeshIndex = dto.submeshIndex;
        submesh.primitiveIndex = dto.primitiveIndex;
    }

    void encodeSky(Rain::Encoder &encoder, Entity entity) {
        const SkyComponent &sky = entity.getComponent<SkyComponent>();
        SkyComponentDto dto;
        dto.isMain = sky.isMain;
        dto.material = sky.material.getId();
        dto.environmentMap = sky.environmentMap.getId();
        dto.environmentIntensity = sky.environmentIntensity;
        dto.ambientColor = sky.ambientColor;
        dto.ambientIntensity = sky.ambientIntensity;
        dto.fog = sky.fog;
        encoder.encode("skyComponent", dto);
    }
    void decodeSky(Rain::Decoder &decoder, Entity entity) {
        SkyComponentDto dto;
        if (!decoder.decode("skyComponent", dto)) { return; }
        SkyComponent &sky = entity.addComponent<SkyComponent>();
        sky.isMain = dto.isMain;
        sky.material = AssetRef<Asset>(dto.material);
        sky.environmentMap = AssetRef<Asset>(dto.environmentMap);
        sky.environmentIntensity = dto.environmentIntensity;
        sky.ambientColor = dto.ambientColor;
        sky.ambientIntensity = dto.ambientIntensity;
        sky.fog = dto.fog;
    }

    void encodeWind(Rain::Encoder &encoder, Entity entity) {
        const WindComponent &wind = entity.getComponent<WindComponent>();
        WindComponentDto dto;
        dto.isMain = wind.isMain;
        dto.strength = wind.strength;
        dto.speed = wind.speed;
        dto.gustiness = wind.gustiness;
        dto.directionDegrees = wind.directionDegrees;
        encoder.encode("windComponent", dto);
    }
    void decodeWind(Rain::Decoder &decoder, Entity entity) {
        WindComponentDto dto;
        if (!decoder.decode("windComponent", dto)) { return; }
        WindComponent &wind = entity.addComponent<WindComponent>();
        wind.isMain = dto.isMain;
        wind.strength = dto.strength;
        wind.speed = dto.speed;
        wind.gustiness = dto.gustiness;
        wind.directionDegrees = dto.directionDegrees;
    }

    void encodePrimitiveMesh(Rain::Encoder &encoder, Entity entity) {
        PrimitiveMeshComponent &primitive = entity.getComponent<PrimitiveMeshComponent>();
        PrimitiveMeshComponentDto dto;
        dto.kind = static_cast<uint32_t>(primitive.kind);
        dto.material = primitive.material.getId();
        encoder.encode("primitiveMeshComponent", dto);
    }
    void decodePrimitiveMesh(Rain::Decoder &decoder, Entity entity) {
        PrimitiveMeshComponentDto dto;
        if (!decoder.decode("primitiveMeshComponent", dto)) { return; }
        PrimitiveMeshComponent &primitive = entity.addComponent<PrimitiveMeshComponent>();
        primitive.kind = static_cast<PrimitiveMeshKind>(dto.kind);
        primitive.material = AssetRef<Asset>(dto.material);
    }

    // A type that is not written to .world: pool only (world copy, duplicate).
    template<typename T>
    ComponentType unserialized(const char *name, bool duplicated = true) {
        ComponentType type = componentType<T>(name, nullptr, nullptr);
        type.serialized = false;
        type.duplicated = duplicated;
        return type;
    }

} // namespace

void registerCoreComponentTypes(ComponentTypeRegistry &registry) {
    // Identity and hierarchy are special: the id is written by the mapper itself (the entity
    // "id"), a duplicate gets a new one; the duplicate rebuilds the relationships
    // (World::duplicateEntity).
    registry.add(unserialized<IdComponent>("idComponent", false));
    ComponentType relationship = componentType<RelationshipComponent>(
        "relationshipComponent", encodeRelationship, decodeRelationship
    );
    relationship.duplicated = false;
    relationship.builtin = true;
    registry.add(relationship);
    auto builtin = [](ComponentType type) {
        type.builtin = true;
        return type;
    };
    registry.add(builtin(componentType<TagComponent>("tagComponent", encodeTag, decodeTag)));
    registry.add(builtin(componentType<TransformComponent>("transformComponent", encodeTransform, decodeTransform)));
    // The kinds a world uses one of (BaseComponents.hpp): the camera, the sun, the sky, the wind.
    registry.add(mainComponentType<CameraComponent>("cameraComponent", encodeCamera, decodeCamera));
    registry.add(mainComponentType<DirectionalLightComponent>(
        "directionalLightComponent", encodeDirectionalLight, decodeDirectionalLight
    ));
    registry.add(componentType<PointLightComponent>("pointLightComponent", encodePointLight, decodePointLight));
    registry.add(componentType<SpriteRendererComponent>("spriteRendererComponent", encodeSprite, decodeSprite));
    registry.add(componentType<ImportedSubmeshComponent>(
        "importedSubmeshComponent", encodeImportedSubmesh, decodeImportedSubmesh
    ));
    registry.add(mainComponentType<SkyComponent>("skyComponent", encodeSky, decodeSky));
    registry.add(mainComponentType<WindComponent>("windComponent", encodeWind, decodeWind));
    registry.add(componentType<PrimitiveMeshComponent>("primitiveMeshComponent", encodePrimitiveMesh, decodePrimitiveMesh));
    ComponentType scripts = componentType<ScriptListComponent>("scriptListComponent", encodeScripts, decodeScripts);
    scripts.entityRefs = scriptEntityRefs;
    registry.add(builtin(scripts));
    registry.add(componentType<IsStaticComponent>("isStatic", encodeStatic, decodeStatic));
    ComponentType prefab = componentType<PrefabComponent>(PREFAB_LINK_KEY, encodePrefab, decodePrefab);
    prefab.entityRefs = prefabEntityRefs;
    registry.add(prefab);
    // Not written: the runtime mesh (lives only in memory), the undo tombstone, the blobs of
    // foreign plugins (the mapper writes them by their own keys).
    registry.add(unserialized<RuntimeMeshComponent>("runtimeMeshComponent"));
    registry.add(unserialized<DeletedComponent>("deletedComponent"));
    registry.add(unserialized<ForeignComponents>("foreignComponents"));
}

} // namespace Panda
