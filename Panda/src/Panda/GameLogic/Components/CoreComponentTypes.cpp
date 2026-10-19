#include "Panda/GameLogic/Components/CoreComponentTypes.hpp"
#include "Panda/GameLogic/Entity.hpp"
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

    void encodeRelationship(Rain::Encoder &encoder, Entity entity) {
        encoder.encode("relationshipComponent", entity.getComponent<RelationshipComponent>());
    }
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
        for (ExternalScriptDto &scriptDto : dto.scripts) {
            std::vector<ScriptField> fields;
            for (ScriptFieldDto &fieldDto : scriptDto.scriptFields) {
                fields.emplace_back(0, fieldDto.fieldId, fieldDto.name, fieldDto.type, fieldDto.value);
            }
            entity.addScript(ExternalScript(entity.getId(), 0, scriptDto.name, fields));
        }
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
        dto.isPrimary = camera.isPrimary;
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
        encoder.encode("cameraComponent", dto);
    }
    void decodeCamera(Rain::Decoder &decoder, Entity entity) {
        CameraComponentDto dto;
        if (!decoder.decode("cameraComponent", dto)) { return; }
        CameraComponent &camera = entity.addComponent<CameraComponent>();
        camera.isPrimary = dto.isPrimary;
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
    }

    void encodeDirectionalLight(Rain::Encoder &encoder, Entity entity) {
        DirectionalLightComponent &light = entity.getComponent<DirectionalLightComponent>();
        DirectionalLightComponentDto dto;
        dto.color = light.color;
        dto.intensity = light.intensity;
        dto.isEnabled = light.isEnabled;
        dto.castShadows = light.castShadows;
        encoder.encode("directionalLightComponent", dto);
    }
    void decodeDirectionalLight(Rain::Decoder &decoder, Entity entity) {
        DirectionalLightComponentDto dto;
        if (!decoder.decode("directionalLightComponent", dto)) { return; }
        DirectionalLightComponent &light = entity.addComponent<DirectionalLightComponent>();
        light.color = dto.color;
        light.intensity = dto.intensity;
        light.isEnabled = dto.isEnabled;
        light.castShadows = dto.castShadows;
    }

    void encodePointLight(Rain::Encoder &encoder, Entity entity) {
        PointLightComponent &light = entity.getComponent<PointLightComponent>();
        PointLightComponentDto dto;
        dto.color = light.color;
        dto.intensity = light.intensity;
        dto.radius = light.radius;
        dto.isEnabled = light.isEnabled;
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
        SkyComponentDto dto;
        dto.material = entity.getComponent<SkyComponent>().material.getId();
        encoder.encode("cubeMapComponent", dto);
    }
    void decodeSky(Rain::Decoder &decoder, Entity entity) {
        SkyComponentDto dto;
        if (!decoder.decode("cubeMapComponent", dto)) { return; }
        entity.addComponent<SkyComponent>().material = AssetRef<Asset>(dto.material);
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
    registry.add(relationship);
    registry.add(componentType<TagComponent>("tagComponent", encodeTag, decodeTag));
    registry.add(componentType<TransformComponent>("transformComponent", encodeTransform, decodeTransform));
    registry.add(componentType<CameraComponent>("cameraComponent", encodeCamera, decodeCamera));
    registry.add(componentType<DirectionalLightComponent>(
        "directionalLightComponent", encodeDirectionalLight, decodeDirectionalLight
    ));
    registry.add(componentType<PointLightComponent>("pointLightComponent", encodePointLight, decodePointLight));
    registry.add(componentType<SpriteRendererComponent>("spriteRendererComponent", encodeSprite, decodeSprite));
    registry.add(componentType<ImportedSubmeshComponent>(
        "importedSubmeshComponent", encodeImportedSubmesh, decodeImportedSubmesh
    ));
    registry.add(componentType<SkyComponent>("cubeMapComponent", encodeSky, decodeSky));
    registry.add(componentType<PrimitiveMeshComponent>("primitiveMeshComponent", encodePrimitiveMesh, decodePrimitiveMesh));
    registry.add(componentType<ScriptListComponent>("scriptListComponent", encodeScripts, decodeScripts));
    registry.add(componentType<IsStaticComponent>("isStatic", encodeStatic, decodeStatic));
    // Not written: the runtime mesh (lives only in memory), the undo tombstone, the blobs of
    // foreign plugins (the mapper writes them by their own keys).
    registry.add(unserialized<RuntimeMeshComponent>("runtimeMeshComponent"));
    registry.add(unserialized<DeletedComponent>("deletedComponent"));
    registry.add(unserialized<ForeignComponents>("foreignComponents"));
}

} // namespace Panda
