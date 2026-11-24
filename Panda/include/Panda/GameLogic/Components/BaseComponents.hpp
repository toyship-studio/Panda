#pragma once

#include "Panda/Renderer/FogSettings.hpp"
#include "Panda/Renderer/Tonemap.hpp"
#include "Panda/Base/Base.hpp"
#include "Panda/Serialization/ForeignValue.hpp"
#include "Panda/GameLogic/Components/WorldCamera.hpp"
#include "Panda/ScriptEngine/ExternalScript.hpp"
#include "Panda/Assets/Base/AssetRef.hpp"

#include <Foundation/Foundation.hpp>

#include <cstdint>

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

// A world has one of each of a few kinds — a camera to look through, a sun, a sky, a wind: the
// component that says so is the MAIN one of its kind (`isMain`). Making one the main one makes
// the others of the world spares (World::setMain; Main in the inspector, a radio button; setMain
// of a script — a sky of the night in the place of the day's, the camera of a cutscene). The
// world uses its main one (World::findMain). A component of such a kind added to a world that
// has none is the main one; one added next to the main one is a spare, and so is a copy that
// joins the world — a duplicate, a paste, an instance of a prefab (World::joinAsSpares). Two
// main ones are a mistake only a file written by hand can hold: the reader of a world says so,
// and so does the editor.

struct CameraComponent final {
    bool isMain = true; // the camera the game looks through
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
    // How the frame reaches the display (the final Tonemap pass): the curve, and the exposure
    // compensation in stops — the whole exposure, or a correction on top of the auto exposure.
    Tonemap tonemap = Tonemap::AgX;
    float exposure = 0.f;
    bool antialiasing = true; // FXAA over the finished frame
    // Screen-space ambient occlusion (the `ssao` feature of the project): the contact darkening of
    // the ambient light the camera sees. What it may cost is the project's
    // (AmbientOcclusionSettings).
    bool ambientOcclusion = true;
    float ambientOcclusionRadius = 0.6f;    // how far an occluder reaches (m)
    float ambientOcclusionIntensity = 1.f;  // 0 — none, 1 — as computed, above — darker
};

// The sun: the light of a direction, along the -Z of its entity. The main one lights the world —
// and the fog around it, the flares — and casts the cascaded shadows; a spare one does nothing
// until it is made the main one (the moon of a night).
struct DirectionalLightComponent final {
    bool isMain = true;
    Color color = Color(1.f, 1.f, 1.f, 1.f);
    float intensity = 1.f;
    bool isEnabled = true;
    bool castShadows = true;    // the cascaded shadows (the maps — ShadowSettings of the project)
    float shadowStrength = 1.f; // 0 — no shadow, 1 — the sun does not reach a shadowed point at all
    // The angular radius of the sun, degrees: the edge of a shadow widens by tan(softness) per
    // meter between the caster and the receiver — sharp at the contact, soft far from it. 0 — the
    // fixed filter of a few texels.
    float shadowSoftness = 1.5f;
};

// A lamp: a light at a point. intensity — what a white matte surface 1 m away, facing it, sends
// back; the light falls with the square of the distance and reaches zero at the radius. With a
// cone it is a spot: it shines along the -Z of its entity, like the sun.
struct PointLightComponent final {
    Color color = Color(1.f, 1.f, 1.f, 1.f);
    float intensity = 1.f;
    float radius = 10.f;
    bool isEnabled = true;
    float coneAngle = 0.f;      // the full angle of the cone, degrees; 0 — no cone: the lamp shines all around
    float coneSoftness = 0.25f; // the share of the cone over which the light fades to its edge, 0..1
    // The lamp casts shadows (the `shadows` feature): a spot costs one more pass over the world
    // every frame, a point light six. Off by default.
    bool castShadows = false;
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

// The sky of a world: what is drawn behind everything, the light of the sky and the air under it.
// The main one is the sky of the world; a world without one is lit by its lamps and its sun alone,
// against black.
struct SkyComponent final {
    bool isMain = true;
    // The material of the sky; empty — the default sky: the environment map, or the built-in
    // gradient without one.
    AssetRef<Asset> material;
    // The environment map (an equirectangular texture, LDR or .hdr): distributed as the system
    // texture environmentMap — the default sky shows it, PBR reflects it, and the light of it is
    // baked for the lit shaders. Empty — the built-in gradient. Intensity — the exposure of the
    // map (the sky and its light together: a night dims it).
    AssetRef<Asset> environmentMap;
    float environmentIntensity = 1.f;
    // A flat light from everywhere, added to the light of the sky.
    Color ambientColor = Color(1.f, 1.f, 1.f, 1.f);
    float ambientIntensity = 0.12f;
    FogSettings fog;
};

// The wind of a world: it sways every plant (the vegetation shaders, the system material fields
// windStrength and windGustiness). The main one blows; a world without one is calm.
struct WindComponent final {
    bool isMain = true;
    float strength = 0.25f;       // 0..1 — overall strength
    float speed = 1.6f;           // tempo of gusts and flutter
    float gustiness = 0.6f;       // contrast: 0 — steady flow, 1 — lulls and squalls
    float directionDegrees = 0.f; // where it blows to, in XZ (0 = +X)
};

} // namespace Panda
