#pragma once

// What the parts of the prefabs of the authoring side (Prefabs.hpp) share: the keys of the
// format, the JSON a patch is made of, the templates, the parts of instances in a world and the
// entries. The parts: PrefabJson (JSON, the patch of an object), PrefabTemplates (a prefab as its
// instances are compared with it; a prefab that changed), PrefabEntries (an entry ↔ the
// entities of an instance; the files) and Prefabs (an instance in a world: what it differs in,
// Revert, Apply, making and unpacking one).
#include "Panda/Prefabs/Prefabs.hpp"

#ifdef PND_EDITOR

#include "Panda/Extensions/Extensions.hpp"
#include "Panda/GameLogic/GameContext.hpp"
#include "Panda/GameLogic/Systems/ScriptSystem.hpp"
#include "Panda/Prefabs/PrefabComponent.hpp"
#include "Panda/Serialization/WorldMapper.hpp"


#include <algorithm>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace Panda::Prefabs::Detail {

inline constexpr const char *ENTITIES = "entities";
inline constexpr const char *ID = "id";
inline constexpr const char *PREFAB = "prefab";
inline constexpr const char *OVERRIDES = "overrides";
inline constexpr const char *RELATIONSHIP = "relationshipComponent";
inline constexpr const char *PARENT = "parent";
inline constexpr const char *CHILDREN = "children";
inline constexpr const char *ENTITY = "entity";
inline constexpr const char *REMOVED = "removed";
inline constexpr const char *TRANSFORM = "transformComponent";
inline constexpr const char *POSITION = "position";
inline constexpr const char *ROTATION = "rotation";
inline constexpr const char *TAG = "tagComponent";
inline constexpr const char *NAME = "name";
inline constexpr const char *MISSING = PREFAB_MISSING_KEY;

/* ------------------------------ JSON (PrefabJson) ------------------------------ */

UUID idOf(const Json &value);
UUID idIn(const Json &object, const char *key);
UUID parentOf(const Json &entity);
// A vector, a quaternion, a color: one value, though the file writes it as an object.
bool isVector(const Json &value);
// An array of objects that all have a name, the names different: the scripts of an entity,
// the fields of a script. Such an array is compared by name. An empty one is of any kind.
bool isNamed(const Json &value);
template<typename JsonType>
JsonType *named(JsonType &array, std::string_view name) {
    for (auto &item : array.items()) {
        const Json *itemName = item.find(NAME);
        if (itemName != nullptr && itemName->isString() && itemName->getString() == name) { return &item; }
    }
    return nullptr;
}
// What an entity object holds besides its components: not a part of what an instance
// differs in.
bool isReserved(const char *key);
// The difference of two entity objects, component by component, added to `patch` (an object).
void diffEntity(const Json &base, const Json &value, Json &patch);
// A place of a patch by a path of keys; nullptr — the patch has nothing there.
Json *descend(Json &patch, const std::vector<std::string> &keys, size_t count);
std::vector<std::string> split(const std::string &path);
// An override that says nothing any more leaves the list; an empty list leaves the entry.
void prune(Json &entry);
// A key of an entry that is not a component the root may differ in: the reserved ones, the
// link, the overrides — and the name, which is the instance's own like its place.
bool isOwnOfRoot(const char *key);

/* ------------------------------ Templates (PrefabTemplates) ------------------------------ */

// A prefab as the instances are compared with it: the entities of its template world as the
// objects a world file would hold, and who is who among them.
struct Template {
    struct Link {
        UUID instance = 0;
        UUID source = 0;
        AssetId prefab = 0;
    };

    AssetId id = 0;
    AssetRef<PrefabAsset> asset; // held: the template lives as long as it is cached
    Json document;           // {"entities": [...]} of the template world
    UUID root = 0;
    std::vector<UUID> ids; // ascending
    std::unordered_map<UUID, const Json *> objects;
    std::unordered_map<UUID, UUID> parents; // 0 for the root
    std::unordered_map<UUID, Link> links;   // the entities that came from a prefab nested in this one
    std::map<std::pair<uint32_t, uint32_t>, UUID> parts; // (nested instance, id in its prefab) → id here
    std::set<AssetId> nested;                            // the prefabs it holds instances of

    std::string nameOf(UUID entity) const {
        const auto object = objects.find(entity);
        const Json *tag = object != objects.end() ? object->second->find(TAG) : nullptr;
        const Json *name = tag != nullptr ? tag->find("tag") : nullptr;
        return name != nullptr && name->isString() ? name->getString() : std::string();
    }
};

bool isBeingRead(AssetId prefab);
const Template *templateOf(AssetId prefab);
// The ids on the way down to an entity of the template: its own id for an entity of the
// prefab file itself, and for an entity of a prefab nested in it — the id of that instance
// in the file, then the way down in its prefab.
bool pathOf(const Template &tmpl, UUID entity, std::vector<UUID> &path);
// The entity of the template a path names; 0 — the prefab has none there.
UUID resolvePath(const Template &tmpl, const std::vector<UUID> &path, size_t from = 0);
Json pathJson(const std::vector<UUID> &path);
std::vector<UUID> pathFrom(const Json &value);

/* ------------------------------ The entities of instances in a world (PrefabEntries) ------------------------------ */

// The entities of one instance by their ids in the prefab — the live ones, and those under a
// tombstone (deleted by an undoable command, or left behind by an entry that has them no more).
struct Parts {
    std::unordered_map<UUID, Entity> live;
    std::unordered_map<UUID, Entity> buried;
};

std::unordered_map<UUID, Parts> partsByInstance(World &world, UUID only = UUID(0));
bool isRootOfInstance(Entity entity);
inline const entt::id_type PREFAB_TYPE = entt::type_hash<PrefabComponent>::value();

// While it lives, the references between the entities of an instance are written the way a
// file has them — by the ids of the prefab: an entity read now is compared with its prefab
// like with like. What it changed it puts back, exactly: nothing may add or remove components
// in between.
class TemplateSpace final {
public:
    TemplateSpace(World &world, UUID instance = UUID(0)) {
        const ComponentTypeRegistry &types = GameContext::getExtensions().components;
        for (auto &[root, parts] : partsByInstance(world, instance)) {
            std::unordered_map<UUID, UUID> sources; // the id in the world → the id in the prefab
            for (auto &[source, entity] : parts.live) { sources[entity.getId()] = source; }
            for (auto &[source, entity] : parts.live) {
                types.forEachReference(entity, [&](UUID &reference) {
                    const auto found = sources.find(reference);
                    if (found == sources.end() || found->second == reference) { return; }
                    m_changed.emplace_back(&reference, reference);
                    reference = found->second;
                }, PREFAB_TYPE);
            }
        }
    }
    ~TemplateSpace() {
        for (auto &[reference, id] : m_changed) { *reference = id; }
    }
    TemplateSpace(const TemplateSpace &) = delete;
    TemplateSpace &operator=(const TemplateSpace &) = delete;

private:
    std::vector<std::pair<UUID *, UUID>> m_changed;
};

// The other way: entities that were just read name the entities of their instance by the ids
// of the prefab — from here on by the ids those have in the world.
void resolveReferences(World &world, UUID instance = UUID(0));
// The object a world file would hold for the entity.
Json objectOf(Entity entity);
// The components of the entity become what the object says: one whose value differs is read
// again, one the object does not have goes (the types every entity has stay). `current` —
// the object the entity has now. The id and the place in the hierarchy are not touched.
void applyObject(Entity entity, const Json &target, const Json &current);

/* ------------------------------ An entry ↔ the entities of an instance (PrefabEntries) ------------------------------ */

struct ExpandedPart {
    UUID source = 0;
    UUID id = 0;
    UUID parent = 0; // in the world; of the root — what its entry says
    Json object;     // without relationshipComponent: the hierarchy is put separately
};

// The entities an entry stands for: those of the prefab with the patches of the entry merged
// in, each with its id (`idFor` — by the id in the prefab) and its parent. The references
// between them are by the ids of the prefab still.
void expandEntry(
    const Template &tmpl, const Json &entry, const std::function<UUID(UUID)> &idFor, std::vector<ExpandedPart> &parts
);
// The entry of an instance from the objects of its entities (each as a world file would
// hold it, the references between them by the ids of the prefab): the prefab, and what
// differs. `isImplied` — the id is of an entity a file does not list (a part of an instance
// that is not its root).
Json buildEntry(
    const Template &tmpl, const Json &root, const std::unordered_map<UUID, const Json *> &parts,
    const std::function<bool(UUID)> &isImplied
);
// The entry of a live instance: its entities are read (the references between them by the
// ids of the prefab) and compared with the prefab. false — not a root, or no prefab.
bool readEntry(Entity root, Json &entry, const Template *&tmpl);

/* ------------------------------ Places of a difference (Prefabs) ------------------------------ */

// Places of the prefab `prefab` taken out of the difference an entry holds — an entry of an
// instance of that prefab, or of a prefab that holds it nested or is a variant of it. true —
// the entry changed.
bool strip(Json &entry, const Json &places, AssetId prefab);

} // namespace Panda::Prefabs::Detail

#endif // PND_EDITOR
