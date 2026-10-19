//
// Created by Admin on 22.07.2023.
//

#pragma once

#include "Panda/Base/Base.hpp"
#include "Panda/Serialization/ForeignValue.hpp"
#include "Panda/GameLogic/Components/WorldCamera.hpp"
#include "Panda/ScriptEngine/ExternalScript.hpp"
#include "Panda/Assets/Base/AssetRef.hpp"

#include <Foundation/Foundation.hpp>

namespace Panda {

// Tombstone: the entity was deleted by an editor command but stays in the registry for undo —
// invisible to the world (World::isValidEntt, isValid); children of a deleted entity count as
// deleted. Set/cleared by AddRemoveEntitiesCommand (Entity::setDeleted); the runtime does not set
// the tag.
struct DeletedComponent final {};

struct IdComponent final : public Rain::Codable {
    UUID id;

    IdComponent() = default;
    IdComponent(const IdComponent &other) = default;
    IdComponent(const UUID &id)
        : id(id) {}

    RAIN_FIELDS_BEGIN(IdComponent)
    RAIN_FIELD(id)
    RAIN_FIELDS_END
};

struct TagComponent final : public Rain::Codable {
    std::string tag;

    TagComponent() = default;
    TagComponent(const TagComponent &other) = default;
    TagComponent(const std::string &tag)
        : tag(tag) {}

    operator std::string &() {
        return tag;
    }
    operator const std::string &() const {
        return tag;
    }

    RAIN_FIELDS_BEGIN(TagComponent)
    RAIN_FIELD(tag)
    RAIN_FIELDS_END
};

// Static tag: the entity's transform and mesh do not change in Play (edits in EDIT are legal —
// consumers rebake by versions). Empty type: entt (ETO) has no instance storage for it, only a
// dense array of entities — `view<IsStaticComponent, …>` iterates the statics without walking the
// world. No consumers yet (the first one — static meshes as Occlusion Volume occluders,
// context/lighting.md, "Open items"; then physics, batching). In .world — the entity's `isStatic`; in the
// editor — "Static" in the inspector header, inherited by children with a prompt (like Unity).
struct IsStaticComponent final {};

// Components of modules that are not loaded: key and value from .world as is (JSON text),
// written back on save — user data survives the absence of the module (the settings of such
// modules are ForeignSettings of the world context).
struct ForeignComponents final {
    std::vector<ForeignValue> items;
};

// The place of an entity in the hierarchy: its parent (0 — a root) and its children in their
// order. Only World writes the links, and World::setParent is the way to change the hierarchy;
// everybody else reads (Entity::getParent, getChildEntities). A world file is loaded and a world
// is copied as a whole, with both sides of every link.
struct RelationshipComponent final : public Rain::Codable {
    RelationshipComponent() = default;
    RelationshipComponent(const RelationshipComponent &other) = default;

    UUID getParent() const {
        return parent;
    }
    const std::vector<UUID> &getChildren() const {
        return children;
    }

    RAIN_FIELDS_BEGIN(RelationshipComponent)
    RAIN_FIELD(parent)
    RAIN_FIELD(children)
    RAIN_FIELDS_END

private:
    UUID parent = 0;
    std::vector<UUID> children;

    friend class World;
};

struct SpriteRendererComponent final {
    // Component
    Color color = Color(0.f, 0.f, 0.f, 1.f);
    int cols = 1;
    int rows = 1;
    int index = 0;
    AssetRef<Asset> material;
    AssetRef<Asset> texture;

    SpriteRendererComponent() = default;
    SpriteRendererComponent(const SpriteRendererComponent &other) = default;
};

struct ImportedSubmeshComponent final {
    AssetRef<Asset> importedMesh;
    // Override on top of the mesh asset's slot layout; empty (the usual case) — the primitive
    // follows the materials of the mesh itself (a mesh tab edit is visible on all instances).
    AssetRef<Asset> material;
    uint32_t submeshIndex;
    uint32_t primitiveIndex;

    ImportedSubmeshComponent() = default;
    ImportedSubmeshComponent(const ImportedSubmeshComponent &other) = default;
};

// Greyboxing primitive: a declarative form — a builtin mesh by kind (the enum is serialized,
// not the internal asset id). Empty material = the built-in checker.
enum class PrimitiveMeshKind : uint32_t {
    Cube = 0,
    Sphere = 1,
    Capsule = 2,
    Plane = 3,
    Cylinder = 4,
};

struct PrimitiveMeshComponent final {
    PrimitiveMeshKind kind = PrimitiveMeshKind::Cube;
    AssetRef<Asset> material;

    PrimitiveMeshComponent() = default;
    PrimitiveMeshComponent(const PrimitiveMeshComponent &other) = default;
};

struct RuntimeMeshComponent final {
    AssetRef<Asset> runtimeMesh;
    AssetRef<Asset> material;

    RuntimeMeshComponent() = default;
    RuntimeMeshComponent(const RuntimeMeshComponent &other) = default;
};

struct CameraComponent final {
    bool isPrimary = true;
    WorldCamera camera;
    AssetRef<Asset> postprocessing;    // PostprocessingAsset — the camera's fullscreen pass chain
    bool postprocessingEnabled = true; // toggled from the game (Bamboo CameraComponentAPI)
    // Built-in bloom (pyramid in the engine, source — the emissive buffer); before the user chain.
    bool bloomEnabled = false;
    float bloomIntensity = 1.f;
    float bloomRadius = 1.f; // spread of the tent filter on the upsample (~0.5..2)
    // Auto exposure / eye adaptation (ExposureRenderer): executed BEFORE bloom.
    bool autoExposureEnabled = false;
    float autoExposureKey = 0.18f;   // target average luminance (gray card)
    float autoExposureSpeed = 2.f;   // adaptation speed, 1/s
    float autoExposureMin = 0.25f;   // exposure limits
    float autoExposureMax = 4.f;
};

struct DirectionalLightComponent final {
    Color color = Color(1.f, 1.f, 1.f, 1.f);
    float intensity = 1.f;
    bool isEnabled = true;
    bool castShadows = true; // cascaded shadows from this sun (the first enabled directional)
};

struct PointLightComponent final {
    Color color = Color(1.f, 1.f, 1.f, 1.f);
    float intensity = 1.f;
    float radius = 10.f;
    bool isEnabled = true;
};

struct ScriptListComponent final {
    std::vector<ExternalScript> scripts;

    void add(const ExternalScript &script) {
        scripts.emplace_back(script);
    }

    void remove(const ExternalScript &script) {
        scripts.erase(std::remove(scripts.begin(), scripts.end(), script), scripts.end());
    }
};

struct SkyComponent final {
    AssetRef<Asset> material;
};

} // namespace Panda
