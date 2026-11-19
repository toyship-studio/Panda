#include "Panda/Prefabs/Prefabs.hpp"

#ifdef PND_EDITOR

#    include "Panda/Extensions/Extensions.hpp"
#    include "Panda/GameLogic/GameContext.hpp"
#    include "Panda/GameLogic/Systems/ScriptSystem.hpp"
#    include "Panda/Prefabs/PrefabComponent.hpp"
#    include "Panda/Serialization/WorldMapper.hpp"

#    include <Rain/Coders/JsonDecoder.hpp>
#    include <Rain/Coders/JsonEncoder.hpp>
#    include <rapidjson/prettywriter.h>
#    include <rapidjson/stringbuffer.h>
#    include <rapidjson/writer.h>

#    include <algorithm>
#    include <map>
#    include <memory>
#    include <set>
#    include <sstream>
#    include <unordered_map>
#    include <unordered_set>

namespace Panda::Prefabs {

namespace {

    using Document = rapidjson::Document;

    constexpr const char *ENTITIES = "entities";
    constexpr const char *ID = "id";
    constexpr const char *PREFAB = "prefab";
    constexpr const char *OVERRIDES = "overrides";
    constexpr const char *RELATIONSHIP = "relationshipComponent";
    constexpr const char *PARENT = "parent";
    constexpr const char *CHILDREN = "children";
    constexpr const char *ENTITY = "entity";
    constexpr const char *REMOVED = "removed";
    constexpr const char *TRANSFORM = "transformComponent";
    constexpr const char *POSITION = "position";
    constexpr const char *ROTATION = "rotation";
    constexpr const char *TAG = "tagComponent";
    constexpr const char *NAME = "name";
    constexpr const char *MISSING = PREFAB_MISSING_KEY;

    /* ------------------------------ JSON ------------------------------ */

    bool parse(const std::string &text, Document &document) {
        document.Parse(text.c_str(), text.size());
        return !document.HasParseError();
    }

    std::string write(const Json &value, bool pretty) {
        rapidjson::StringBuffer buffer;
        if (pretty) {
            rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer); // the format of Rain::JsonEncoder
            value.Accept(writer);
        } else {
            rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
            value.Accept(writer);
        }
        return std::string(buffer.GetString(), buffer.GetSize());
    }

    Json copyOf(const Json &value, JsonAllocator &allocator) {
        return Json(value, allocator, /*copyConstStrings*/ true);
    }

    void add(Json &object, const char *key, Json value, JsonAllocator &allocator) {
        object.AddMember(Json(key, allocator).Move(), value, allocator);
    }

    void set(Json &object, const char *key, Json value, JsonAllocator &allocator) {
        const auto member = object.FindMember(key);
        if (member != object.MemberEnd()) {
            member->value = value;
        } else {
            add(object, key, std::move(value), allocator);
        }
    }

    void erase(Json &object, const char *key) {
        const auto member = object.FindMember(key);
        if (member != object.MemberEnd()) { object.EraseMember(member); } // the order of the rest stays
    }

    const Json *find(const Json &object, const char *key) {
        if (!object.IsObject()) { return nullptr; }
        const auto member = object.FindMember(key);
        return member != object.MemberEnd() ? &member->value : nullptr;
    }
    Json *find(Json &object, const char *key) {
        return const_cast<Json *>(find(static_cast<const Json &>(object), key));
    }

    UUID idOf(const Json &value) {
        return value.IsUint() ? UUID(value.GetUint()) : UUID(0);
    }
    UUID idIn(const Json &object, const char *key) {
        const Json *value = find(object, key);
        return value != nullptr ? idOf(*value) : UUID(0);
    }
    UUID parentOf(const Json &entity) {
        const Json *relationship = find(entity, RELATIONSHIP);
        return relationship != nullptr ? idIn(*relationship, PARENT) : UUID(0);
    }

    // A vector, a quaternion, a color: one value, though the file writes it as an object.
    bool isVector(const Json &value) {
        if (!value.IsObject() || value.MemberCount() == 0) { return false; }
        for (auto member = value.MemberBegin(); member != value.MemberEnd(); ++member) {
            const char *name = member->name.GetString();
            if (member->name.GetStringLength() != 1 || (name[0] != 'x' && name[0] != 'y' && name[0] != 'z' && name[0] != 'w')) {
                return false;
            }
        }
        return true;
    }

    // An array of objects that all have a name, the names different: the scripts of an entity,
    // the fields of a script. Such an array is compared by name. An empty one is of any kind.
    bool isNamed(const Json &value) {
        if (!value.IsArray()) { return false; }
        std::set<std::string> names;
        for (const Json &item : value.GetArray()) {
            const Json *name = find(item, NAME);
            if (name == nullptr || !name->IsString() || !names.insert(name->GetString()).second) { return false; }
        }
        return true;
    }

    template<typename JsonType>
    JsonType *named(JsonType &array, const char *name) {
        for (auto &item : array.GetArray()) {
            const Json *itemName = find(item, NAME);
            if (itemName != nullptr && itemName->IsString() && std::strcmp(itemName->GetString(), name) == 0) { return &item; }
        }
        return nullptr;
    }

    // What an entity object holds besides its components: not a part of what an instance
    // differs in.
    bool isReserved(const char *key) {
        return std::strcmp(key, ID) == 0 || std::strcmp(key, RELATIONSHIP) == 0 || std::strcmp(key, PREFAB_LINK_KEY) == 0 ||
               std::strcmp(key, MISSING) == 0;
    }

    // The difference of two entity objects, component by component, added to `patch` (an object).
    void diffEntity(const Json &base, const Json &value, Json &patch, JsonAllocator &allocator) {
        for (auto member = value.MemberBegin(); member != value.MemberEnd(); ++member) {
            const char *key = member->name.GetString();
            if (isReserved(key)) { continue; }
            const Json *baseValue = find(base, key);
            Json part;
            if (baseValue == nullptr) {
                part = copyOf(member->value, allocator);
            } else if (!diff(*baseValue, member->value, part, allocator)) {
                continue;
            }
            add(patch, key, std::move(part), allocator);
        }
        for (auto member = base.MemberBegin(); member != base.MemberEnd(); ++member) {
            const char *key = member->name.GetString();
            if (!isReserved(key) && find(value, key) == nullptr) { add(patch, key, Json(), allocator); }
        }
    }

    /* ------------------------------ Entities as JSON ------------------------------ */

    // The object a world file would hold for the entity.
    Json objectOf(Entity entity, JsonAllocator &allocator) {
        Rain::JsonEncoder encoder(false);
        std::ostringstream text;
        WorldMapper::saveEntity(entity, encoder, text);
        Document document;
        if (!parse(text.str(), document)) { return Json(rapidjson::kObjectType); }
        return copyOf(document, allocator);
    }

    // The components of the entity become what the object says: one whose value differs is read
    // again, one the object does not have goes (the types every entity has stay). `current` —
    // the object the entity has now. The id and the place in the hierarchy are not touched.
    void applyObject(Entity entity, const Json &target, const Json &current) {
        const ComponentTypeRegistry &types = GameContext::getExtensions().components;
        std::vector<ForeignValue> foreign;
        for (auto member = target.MemberBegin(); member != target.MemberEnd(); ++member) {
            const char *key = member->name.GetString();
            if (std::strcmp(key, ID) == 0 || std::strcmp(key, RELATIONSHIP) == 0) { continue; }
            const ComponentType *type = types.find(key);
            if (type == nullptr) {
                foreign.push_back({key, write(member->value, false)});
                continue;
            }
            const Json *now = find(current, key);
            if (now != nullptr && *now == member->value) { continue; }
            // The text of one component: `{"<type>": <value>}`.
            Document component(rapidjson::kObjectType);
            add(component, key, copyOf(member->value, component.GetAllocator()), component.GetAllocator());
            Rain::JsonDecoder decoder;
            std::istringstream stream(write(component, false));
            WorldMapper::loadComponent(entity, *type, decoder, stream);
        }
        for (const ComponentType &type : types.types()) {
            if (!type.serialized || type.builtin || type.remove == nullptr) { continue; }
            if (entity.hasComponent(type.typeId) && find(target, type.name.c_str()) == nullptr) { type.remove(entity); }
        }
        // The components of modules that are not loaded are carried as text.
        if (foreign.empty()) {
            if (entity.hasComponent<ForeignComponents>()) { entity.removeComponent<ForeignComponents>(); }
        } else {
            ForeignComponents &kept = entity.hasComponent<ForeignComponents>() ? entity.getComponent<ForeignComponents>()
                                                                             : entity.addComponent<ForeignComponents>();
            kept.items = std::move(foreign);
        }
    }

    /* ------------------------------ Templates ------------------------------ */

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
        Document document;           // {"entities": [...]} of the template world
        UUID root = 0;
        std::vector<UUID> ids; // ascending
        std::unordered_map<UUID, const Json *> objects;
        std::unordered_map<UUID, UUID> parents; // 0 for the root
        std::unordered_map<UUID, Link> links;   // the entities that came from a prefab nested in this one
        std::map<std::pair<uint32_t, uint32_t>, UUID> parts; // (nested instance, id in its prefab) → id here
        std::set<AssetId> nested;                            // the prefabs it holds instances of

        std::string nameOf(UUID entity) const {
            const auto object = objects.find(entity);
            const Json *tag = object != objects.end() ? find(*object->second, TAG) : nullptr;
            const Json *name = tag != nullptr ? find(*tag, "tag") : nullptr;
            return name != nullptr && name->IsString() ? name->GetString() : std::string();
        }
    };

    std::unordered_map<AssetId, std::unique_ptr<Template>> s_templates;
    // The prefabs being read right now: one that asks for itself on the way holds itself.
    std::vector<AssetId> s_loading;
    // prefabChanged: the prefabs to be read again when next asked for, and who reads them.
    std::unordered_set<AssetId> s_stale;
    const std::function<void(AssetId)> *s_reload = nullptr;

    bool isBeingRead(AssetId prefab) {
        return std::find(s_loading.begin(), s_loading.end(), prefab) != s_loading.end();
    }

    const Template *templateOf(AssetId prefab) {
        if (prefab == AssetId(0)) { return nullptr; }
        if (isBeingRead(prefab)) { return nullptr; } // it holds itself: whoever asked says so
        if (s_stale.erase(prefab) > 0) {
            s_templates.erase(prefab);
            if (s_reload != nullptr) { (*s_reload)(prefab); }
        }
        const auto cached = s_templates.find(prefab);
        if (cached != s_templates.end()) { return cached->second.get(); }
        AssetHandler *assets = GameContext::getAssetHandler();
        if (assets == nullptr || !assets->hasAsset(prefab)) { return nullptr; }
        AssetRef<PrefabAsset> reference(prefab);
        PrefabAsset *asset = reference.get();
        if (asset == nullptr) { return nullptr; } // an asset of another kind
        Entity root = asset->root();
        if (!root.isValid()) { return nullptr; }

        auto made = std::make_unique<Template>();
        made->id = prefab;
        made->asset = reference;
        made->root = root.getId();
        Rain::JsonEncoder encoder(false);
        std::ostringstream text;
        WorldMapper::saveTree(asset->world(), root, encoder, text);
        if (!parse(text.str(), made->document)) { return nullptr; }
        const Json *entities = find(made->document, ENTITIES);
        if (entities == nullptr || !entities->IsArray()) { return nullptr; }
        for (const Json &object : entities->GetArray()) {
            const UUID id = idIn(object, ID);
            made->ids.push_back(id);
            made->objects[id] = &object;
            made->parents[id] = id == made->root ? UUID(0) : parentOf(object);
            if (const Json *link = find(object, PREFAB_LINK_KEY)) {
                Template::Link nested;
                nested.instance = idIn(*link, "instance");
                nested.source = idIn(*link, "source");
                nested.prefab = idIn(*link, "prefab");
                made->links[id] = nested;
                made->parts[{nested.instance, nested.source}] = id;
                if (nested.prefab != AssetId(0)) { made->nested.insert(nested.prefab); }
            }
        }
        std::sort(made->ids.begin(), made->ids.end());
        return (s_templates[prefab] = std::move(made)).get();
    }

    // The ids on the way down to an entity of the template: its own id for an entity of the
    // prefab file itself, and for an entity of a prefab nested in it — the id of that instance
    // in the file, then the way down in its prefab.
    bool pathOf(const Template &tmpl, UUID entity, std::vector<UUID> &path) {
        const auto link = tmpl.links.find(entity);
        if (link == tmpl.links.end() || link->second.instance == entity) {
            path.push_back(entity);
            return tmpl.objects.contains(entity);
        }
        const auto root = tmpl.links.find(link->second.instance);
        const Template *nested = root != tmpl.links.end() ? templateOf(root->second.prefab) : nullptr;
        path.push_back(link->second.instance);
        return nested != nullptr && pathOf(*nested, link->second.source, path);
    }

    // The entity of the template a path names; 0 — the prefab has none there.
    UUID resolvePath(const Template &tmpl, const std::vector<UUID> &path, size_t from = 0) {
        if (from >= path.size()) { return UUID(0); }
        if (from + 1 == path.size()) { return tmpl.objects.contains(path[from]) ? path[from] : UUID(0); }
        const auto root = tmpl.links.find(path[from]);
        const Template *nested = root != tmpl.links.end() ? templateOf(root->second.prefab) : nullptr;
        if (nested == nullptr) { return UUID(0); }
        const UUID inner = resolvePath(*nested, path, from + 1);
        const auto part = tmpl.parts.find({static_cast<uint32_t>(path[from]), static_cast<uint32_t>(inner)});
        return inner != UUID(0) && part != tmpl.parts.end() ? part->second : UUID(0);
    }

    Json pathJson(const std::vector<UUID> &path, JsonAllocator &allocator) {
        if (path.size() == 1) { return Json(static_cast<uint32_t>(path.front())); }
        Json array(rapidjson::kArrayType);
        for (UUID id : path) { array.PushBack(static_cast<uint32_t>(id), allocator); }
        return array;
    }

    std::vector<UUID> pathFrom(const Json &value) {
        std::vector<UUID> path;
        if (value.IsUint()) {
            path.push_back(UUID(value.GetUint()));
        } else if (value.IsArray()) {
            for (const Json &item : value.GetArray()) { path.push_back(idOf(item)); }
        }
        return path;
    }

    /* ------------------------------ The entities of instances in a world ------------------------------ */

    // The entities of one instance by their ids in the prefab — the live ones, and those under a
    // tombstone (deleted by an undoable command, or left behind by an entry that has them no more).
    struct Parts {
        std::unordered_map<UUID, Entity> live;
        std::unordered_map<UUID, Entity> buried;
    };

    std::unordered_map<UUID, Parts> partsByInstance(World &world, UUID only = UUID(0)) {
        std::unordered_map<UUID, Parts> instances;
        auto view = world.getAllEntitiesWith<PrefabComponent>();
        for (auto handle : view) {
            const PrefabComponent &link = view.get<PrefabComponent>(handle);
            if (only != UUID(0) && link.instance != only) { continue; }
            Parts &parts = instances[link.instance];
            (world.isValidEntt(handle) ? parts.live : parts.buried)[link.source] = world.entity(handle);
        }
        return instances;
    }

    bool isRootOfInstance(Entity entity) {
        return entity.isValid() && entity.hasComponent<PrefabComponent>() &&
               entity.getComponent<PrefabComponent>().instance == entity.getId();
    }

    const entt::id_type PREFAB_TYPE = entt::type_hash<PrefabComponent>::value();

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
                    for (const ComponentType &type : types.types()) {
                        if (type.entityRefs == nullptr || type.typeId == PREFAB_TYPE || !entity.hasComponent(type.typeId)) { continue; }
                        type.entityRefs(entity, [&](UUID &reference) {
                            const auto found = sources.find(reference);
                            if (found == sources.end() || found->second == reference) { return; }
                            m_changed.emplace_back(&reference, reference);
                            reference = found->second;
                        });
                    }
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
    void resolveReferences(World &world, UUID instance = UUID(0)) {
        const ComponentTypeRegistry &types = GameContext::getExtensions().components;
        for (auto &[root, parts] : partsByInstance(world, instance)) {
            std::unordered_map<UUID, UUID> ids; // the id in the prefab → the id in the world
            for (auto &[source, entity] : parts.live) { ids[source] = entity.getId(); }
            for (auto &[source, entity] : parts.live) {
                for (const ComponentType &type : types.types()) {
                    if (type.entityRefs == nullptr || type.typeId == PREFAB_TYPE || !entity.hasComponent(type.typeId)) { continue; }
                    type.entityRefs(entity, [&](UUID &reference) {
                        const auto found = ids.find(reference);
                        if (found != ids.end()) { reference = found->second; }
                    });
                }
            }
        }
    }

    /* ------------------------------ An entry ↔ the entities of an instance ------------------------------ */

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
        const Template &tmpl, const Json &entry, const std::function<UUID(UUID)> &idFor, std::vector<ExpandedPart> &parts,
        JsonAllocator &allocator
    ) {
        const UUID instance = idIn(entry, ID);
        // What the entry says of the entities under the root, by their ids in the prefab.
        std::unordered_map<UUID, const Json *> overrides;
        if (const Json *list = find(entry, OVERRIDES); list != nullptr && list->IsArray()) {
            for (const Json &item : list->GetArray()) {
                const Json *named = find(item, ENTITY);
                const UUID source = named != nullptr ? resolvePath(tmpl, pathFrom(*named)) : UUID(0);
                if (source == UUID(0) || source == tmpl.root) {
                    LOG_WARN_EDITOR(
                        "The instance %u of %s overrides an entity the prefab does not have (%s): the override is left out.",
                        static_cast<uint32_t>(instance), tmpl.nameOf(tmpl.root).c_str(), named != nullptr ? write(*named, false).c_str() : "?"
                    );
                    continue;
                }
                overrides[source] = &item;
            }
        }
        // An entity is in the instance unless it was removed — itself, or something above it that
        // it was not moved out of.
        const std::function<bool(UUID)> isThere = [&](UUID source) {
            if (source == tmpl.root) { return true; }
            const auto found = overrides.find(source);
            if (found != overrides.end()) {
                const Json *removed = find(*found->second, REMOVED);
                if (removed != nullptr && removed->IsBool() && removed->GetBool()) { return false; }
                if (find(*found->second, PARENT) != nullptr) { return true; }
            }
            return isThere(tmpl.parents.at(source));
        };
        for (UUID source : tmpl.ids) {
            if (!isThere(source)) { continue; }
            ExpandedPart part;
            part.source = source;
            part.id = source == tmpl.root ? instance : idFor(source);
            part.object = copyOf(*tmpl.objects.at(source), allocator);
            const auto found = overrides.find(source);
            if (source == tmpl.root) {
                Json patch(rapidjson::kObjectType);
                for (auto member = entry.MemberBegin(); member != entry.MemberEnd(); ++member) {
                    const char *key = member->name.GetString();
                    if (isReserved(key) || std::strcmp(key, PREFAB) == 0 || std::strcmp(key, OVERRIDES) == 0) { continue; }
                    add(patch, key, copyOf(member->value, allocator), allocator);
                }
                merge(part.object, patch, allocator);
                part.parent = parentOf(entry);
            } else {
                const Json *placed = nullptr;
                if (found != overrides.end()) {
                    Json patch(rapidjson::kObjectType);
                    for (auto member = found->second->MemberBegin(); member != found->second->MemberEnd(); ++member) {
                        const char *key = member->name.GetString();
                        if (isReserved(key) || std::strcmp(key, ENTITY) == 0 || std::strcmp(key, REMOVED) == 0 || std::strcmp(key, PARENT) == 0) {
                            continue;
                        }
                        add(patch, key, copyOf(member->value, allocator), allocator);
                    }
                    merge(part.object, patch, allocator);
                    placed = find(*found->second, PARENT);
                }
                const UUID above = tmpl.parents.at(source);
                part.parent = placed != nullptr ? idOf(*placed) : above == tmpl.root ? instance : idFor(above);
            }
            // The object is of this instance now: its id, and where it came from. The root of an
            // instance nested in the prefab keeps the prefab it is of.
            const auto nested = tmpl.links.find(source);
            Json link(rapidjson::kObjectType);
            add(link, "instance", Json(static_cast<uint32_t>(instance)), allocator);
            add(link, "source", Json(static_cast<uint32_t>(source)), allocator);
            add(
                link, "prefab",
                Json(static_cast<uint32_t>(source == tmpl.root ? tmpl.id : nested != tmpl.links.end() ? nested->second.prefab : AssetId(0))),
                allocator
            );
            set(part.object, ID, Json(static_cast<uint32_t>(part.id)), allocator);
            erase(part.object, RELATIONSHIP);
            set(part.object, PREFAB_LINK_KEY, std::move(link), allocator);
            parts.push_back(std::move(part));
        }
    }

    // The entry of an instance from the objects of its entities (each as a world file would
    // hold it, the references between them by the ids of the prefab): the prefab, and what
    // differs. `isImplied` — the id is of an entity a file does not list (a part of an instance
    // that is not its root).
    Json buildEntry(
        const Template &tmpl, const Json &root, const std::unordered_map<UUID, const Json *> &parts,
        const std::function<bool(UUID)> &isImplied, JsonAllocator &allocator
    ) {
        Json entry(rapidjson::kObjectType);
        add(entry, ID, Json(static_cast<uint32_t>(idIn(root, ID))), allocator);
        add(entry, PREFAB, Json(static_cast<uint32_t>(tmpl.id)), allocator);
        // Its place in the world, and the entities of the file under its root.
        Json relationship(rapidjson::kObjectType);
        add(relationship, PARENT, Json(static_cast<uint32_t>(parentOf(root))), allocator);
        Json children(rapidjson::kArrayType);
        const Json *rootRelationship = find(root, RELATIONSHIP);
        if (const Json *listed = rootRelationship != nullptr ? find(*rootRelationship, CHILDREN) : nullptr; listed != nullptr && listed->IsArray()) {
            for (const Json &child : listed->GetArray()) {
                if (!isImplied(idOf(child))) { children.PushBack(static_cast<uint32_t>(idOf(child)), allocator); }
            }
        }
        add(relationship, CHILDREN, std::move(children), allocator);
        add(entry, RELATIONSHIP, std::move(relationship), allocator);
        // What the root differs in. What it is called and where it stands are its own: the
        // name, the position and the rotation are always written, whatever the prefab has —
        // first, and in one order, so that an entry does not change its lines when a value
        // starts or stops being the one of the prefab.
        Json patch(rapidjson::kObjectType);
        diffEntity(*tmpl.objects.at(tmpl.root), root, patch, allocator);
        if (const Json *tag = find(root, TAG)) { add(entry, TAG, copyOf(*tag, allocator), allocator); }
        if (const Json *transform = find(root, TRANSFORM)) {
            Json own(rapidjson::kObjectType);
            for (const char *key : {POSITION, ROTATION}) {
                if (const Json *value = find(*transform, key)) { add(own, key, copyOf(*value, allocator), allocator); }
            }
            if (Json *differing = find(patch, TRANSFORM); differing != nullptr && differing->IsObject()) {
                for (auto member = differing->MemberBegin(); member != differing->MemberEnd(); ++member) {
                    const char *key = member->name.GetString();
                    if (std::strcmp(key, POSITION) != 0 && std::strcmp(key, ROTATION) != 0) { own.AddMember(member->name, member->value, allocator); }
                }
            }
            add(entry, TRANSFORM, std::move(own), allocator);
        }
        for (auto member = patch.MemberBegin(); member != patch.MemberEnd(); ++member) {
            const char *key = member->name.GetString();
            if (std::strcmp(key, TAG) != 0 && std::strcmp(key, TRANSFORM) != 0) { entry.AddMember(member->name, member->value, allocator); }
        }
        // What the entities under it differ in.
        Json overrides(rapidjson::kArrayType);
        for (UUID source : tmpl.ids) {
            if (source == tmpl.root) { continue; }
            const auto part = parts.find(source);
            const auto above = parts.find(tmpl.parents.at(source));
            std::vector<UUID> path;
            if (!pathOf(tmpl, source, path)) { continue; }
            if (part == parts.end()) {
                // Removed — said for the topmost one: what was under it went with it.
                if (above != parts.end()) {
                    Json removed(rapidjson::kObjectType);
                    add(removed, ENTITY, pathJson(path, allocator), allocator);
                    add(removed, REMOVED, Json(true), allocator);
                    overrides.PushBack(removed, allocator);
                }
                continue;
            }
            Json item(rapidjson::kObjectType);
            add(item, ENTITY, pathJson(path, allocator), allocator);
            diffEntity(*tmpl.objects.at(source), *part->second, item, allocator);
            // It stands under another entity than the prefab says — or what it stood under is gone.
            const UUID parent = parentOf(*part->second);
            if (above == parts.end() || parent != idIn(*above->second, ID)) {
                add(item, PARENT, Json(static_cast<uint32_t>(parent)), allocator);
            }
            if (item.MemberCount() > 1) { overrides.PushBack(item, allocator); }
        }
        if (!overrides.Empty()) { add(entry, OVERRIDES, std::move(overrides), allocator); }
        return entry;
    }

    // A place of a patch by a path of keys; nullptr — the patch has nothing there.
    Json *descend(Json &patch, const std::vector<std::string> &keys, size_t count) {
        Json *at = &patch;
        for (size_t i = 0; i < count && at != nullptr; i++) { at = find(*at, keys[i].c_str()); }
        return at;
    }

    std::vector<std::string> split(const std::string &path) {
        std::vector<std::string> keys;
        size_t start = 0;
        while (start <= path.size()) {
            const size_t slash = path.find('/', start);
            const size_t end = slash == std::string::npos ? path.size() : slash;
            if (end > start) { keys.push_back(path.substr(start, end - start)); }
            if (slash == std::string::npos) { break; }
            start = slash + 1;
        }
        return keys;
    }

    // Takes a place out of a patch (an object of an entry: the entry itself for the root, an
    // item of its overrides): the keys on the way that are left empty go too.
    void erasePlace(Json &object, const std::string &component, const std::string &field) {
        std::vector<std::string> keys{component};
        for (std::string &key : split(field)) { keys.push_back(std::move(key)); }
        for (size_t count = keys.size(); count > 0; count--) {
            Json *owner = descend(object, keys, count - 1);
            if (owner == nullptr || !owner->IsObject()) { continue; }
            const Json *value = find(*owner, keys[count - 1].c_str());
            if (value == nullptr) { continue; }
            // Below the place that was asked for, a key goes only when it holds nothing any more.
            if (count < keys.size() && !(value->IsObject() && value->MemberCount() == 0)) { break; }
            erase(*owner, keys[count - 1].c_str());
        }
    }

    // The item of the overrides of an entry that speaks of the path; nullptr — none.
    Json *overrideAt(Json &entry, const std::vector<UUID> &path) {
        Json *list = find(entry, OVERRIDES);
        if (list == nullptr || !list->IsArray()) { return nullptr; }
        for (Json &item : list->GetArray()) {
            const Json *named = find(item, ENTITY);
            if (named != nullptr && pathFrom(*named) == path) { return &item; }
        }
        return nullptr;
    }

    // An override that says nothing any more leaves the list; an empty list leaves the entry.
    void prune(Json &entry) {
        Json *list = find(entry, OVERRIDES);
        if (list == nullptr || !list->IsArray()) { return; }
        for (auto item = list->Begin(); item != list->End();) {
            item = item->MemberCount() <= 1 ? list->Erase(item) : item + 1;
        }
        if (list->Empty()) { erase(entry, OVERRIDES); }
    }

    // A key of an entry that is not a component the root may differ in: the reserved ones, the
    // link, the overrides — and the name, which is the instance's own like its place.
    bool isOwnOfRoot(const char *key) {
        return isReserved(key) || std::strcmp(key, PREFAB) == 0 || std::strcmp(key, OVERRIDES) == 0 || std::strcmp(key, TAG) == 0;
    }

    // The component keys of the root in its entry, its name and its place aside.
    void eraseRootDifference(Json &entry) {
        std::vector<std::string> keys;
        for (auto member = entry.MemberBegin(); member != entry.MemberEnd(); ++member) {
            const char *key = member->name.GetString();
            if (isOwnOfRoot(key)) { continue; }
            keys.emplace_back(key);
        }
        for (const std::string &key : keys) {
            if (key != TRANSFORM) {
                erase(entry, key.c_str());
                continue;
            }
            Json &transform = *find(entry, TRANSFORM);
            std::vector<std::string> inner;
            for (auto member = transform.MemberBegin(); member != transform.MemberEnd(); ++member) {
                const char *name = member->name.GetString();
                if (std::strcmp(name, POSITION) != 0 && std::strcmp(name, ROTATION) != 0) { inner.emplace_back(name); }
            }
            for (const std::string &name : inner) { erase(transform, name.c_str()); }
        }
    }

    // The entry says something of the root besides what it is called and where it stands.
    bool rootDiffers(const Json &entry) {
        for (auto member = entry.MemberBegin(); member != entry.MemberEnd(); ++member) {
            const char *key = member->name.GetString();
            if (isOwnOfRoot(key)) { continue; }
            if (std::strcmp(key, TRANSFORM) != 0) { return true; }
            for (auto inner = member->value.MemberBegin(); inner != member->value.MemberEnd(); ++inner) {
                const char *name = inner->name.GetString();
                if (std::strcmp(name, POSITION) != 0 && std::strcmp(name, ROTATION) != 0) { return true; }
            }
        }
        return false;
    }

    // The entry of a live instance: its entities are read (the references between them by the
    // ids of the prefab) and compared with the prefab. false — not a root, or no prefab.
    bool readEntry(Entity root, Document &entry, const Template *&tmpl) {
        if (!isRootOfInstance(root)) { return false; }
        tmpl = templateOf(root.getComponent<PrefabComponent>().prefab);
        if (tmpl == nullptr) { return false; }
        World &world = *root.getWorld();
        Parts parts = partsByInstance(world, root.getId())[root.getId()];
        Document objects; // owns the objects of the entities
        std::unordered_map<UUID, Json> read;
        {
            TemplateSpace space(world, root.getId());
            for (auto &[source, entity] : parts.live) { read[source] = objectOf(entity, objects.GetAllocator()); }
        }
        std::unordered_map<UUID, const Json *> byId;
        for (auto &[source, object] : read) { byId[source] = &object; }
        const auto rootObject = byId.find(tmpl->root);
        if (rootObject == byId.end()) { return false; }
        const auto isImplied = [&world](UUID id) {
            Entity child = world.getById(id);
            return child.isValid() && child.hasComponent<PrefabComponent>() && child.getComponent<PrefabComponent>().instance != id;
        };
        entry.SetObject();
        static_cast<Json &>(entry) = buildEntry(*tmpl, *rootObject->second, byId, isImplied, entry.GetAllocator());
        return true;
    }

    // The places of a component that differ, as paths of keys: the patch is followed down while
    // the prefab has an object (or a list by name) there.
    void collectFields(const Json &base, const Json &patch, const std::string &prefix, std::vector<std::string> &fields) {
        const bool object = base.IsObject() && !isVector(base);
        const bool list = base.IsArray() && isNamed(base);
        if (!patch.IsObject() || isVector(patch) || (!object && !list)) {
            fields.push_back(prefix);
            return;
        }
        for (auto member = patch.MemberBegin(); member != patch.MemberEnd(); ++member) {
            const char *key = member->name.GetString();
            const Json *inner = object ? find(base, key) : named(base, key);
            const std::string path = prefix.empty() ? key : prefix + "/" + key;
            if (inner == nullptr || member->value.IsNull()) {
                fields.push_back(path);
            } else {
                collectFields(*inner, member->value, path, fields);
            }
        }
    }

    // What an Apply took, kept as `[{"entity": <path>, …patch}]`: the root of the prefab is
    // named like any entity of it.
    void notePatch(Json &patches, const Template &tmpl, UUID source, const Json &patch, JsonAllocator &allocator) {
        std::vector<UUID> path;
        if (!pathOf(tmpl, source, path)) { return; }
        Json item(rapidjson::kObjectType);
        add(item, ENTITY, pathJson(path, allocator), allocator);
        for (auto member = patch.MemberBegin(); member != patch.MemberEnd(); ++member) {
            add(item, member->name.GetString(), copyOf(member->value, allocator), allocator);
        }
        patches.PushBack(item, allocator);
    }

    // The keys of `applied` taken out of `patch`, place by place: a place the applied patch
    // gave a value loses its own here.
    void subtract(Json &patch, const Json &applied) {
        if (!patch.IsObject() || !applied.IsObject() || isVector(patch) || isVector(applied)) { return; }
        std::vector<std::string> gone;
        for (auto member = applied.MemberBegin(); member != applied.MemberEnd(); ++member) {
            const char *key = member->name.GetString();
            if (std::strcmp(key, ENTITY) == 0) { continue; }
            Json *own = find(patch, key);
            if (own == nullptr) { continue; }
            if (own->IsObject() && member->value.IsObject() && !isVector(*own) && !isVector(member->value)) {
                subtract(*own, member->value);
                if (own->MemberCount() == 0) { gone.emplace_back(key); }
            } else {
                gone.emplace_back(key);
            }
        }
        for (const std::string &key : gone) { erase(patch, key.c_str()); }
    }

    // Takes the applied keys out of one entry (of any prefab: an instance of a prefab that
    // nests the applied one overrides its entities by a longer path). true — the entry changed.
    bool stripEntry(Json &entry, const Json &patches, AssetId prefab) {
        const Template *tmpl = templateOf(idIn(entry, PREFAB));
        if (tmpl == nullptr) { return false; }
        const std::string before = write(entry, false);
        for (const Json &applied : patches.GetArray()) {
            const std::vector<UUID> target = pathFrom(*find(applied, ENTITY));
            // The root of the applied prefab: its difference is the entry itself.
            if (tmpl->id == prefab && target.size() == 1 && target.front() == tmpl->root) {
                subtract(entry, applied); // an applied patch never holds the place of its instance
                continue;
            }
            Json *list = find(entry, OVERRIDES);
            if (list == nullptr || !list->IsArray()) { continue; }
            for (Json &item : list->GetArray()) {
                const Json *named = find(item, ENTITY);
                if (named == nullptr) { continue; }
                const std::vector<UUID> path = pathFrom(*named);
                bool matches = false;
                // The path of the override leads into the applied prefab, and what is left of it
                // there names the applied entity. The root of a nested instance is named by the
                // id of that instance alone.
                for (size_t depth = 0; depth < path.size() && !matches; depth++) {
                    const Template *at = tmpl;
                    bool valid = true;
                    for (size_t step = 0; step < depth && valid; step++) {
                        const auto root = at->links.find(path[step]);
                        at = root != at->links.end() ? templateOf(root->second.prefab) : nullptr;
                        valid = at != nullptr;
                    }
                    if (!valid) { break; }
                    if (at->id == prefab && std::vector<UUID>(path.begin() + static_cast<std::ptrdiff_t>(depth), path.end()) == target) { matches = true; }
                    // …or ends on the nested instance itself, which is the root of the applied prefab.
                    if (depth + 1 == path.size()) {
                        const auto root = at->links.find(path[depth]);
                        const Template *nested = root != at->links.end() ? templateOf(root->second.prefab) : nullptr;
                        if (nested != nullptr && nested->id == prefab && target.size() == 1 && target.front() == nested->root) { matches = true; }
                    }
                }
                if (matches) { subtract(item, applied); }
            }
        }
        prune(entry);
        return write(entry, false) != before;
    }

} // namespace

/* ------------------------------ The patch of an object ------------------------------ */

bool diff(const Json &base, const Json &value, Json &patch, JsonAllocator &allocator) {
    if (base.IsObject() && value.IsObject() && !isVector(base) && !isVector(value)) {
        Json result(rapidjson::kObjectType);
        for (auto member = value.MemberBegin(); member != value.MemberEnd(); ++member) {
            const Json *baseValue = find(base, member->name.GetString());
            Json part;
            if (baseValue == nullptr) {
                part = copyOf(member->value, allocator);
            } else if (!diff(*baseValue, member->value, part, allocator)) {
                continue;
            }
            add(result, member->name.GetString(), std::move(part), allocator);
        }
        for (auto member = base.MemberBegin(); member != base.MemberEnd(); ++member) {
            if (find(value, member->name.GetString()) == nullptr) { add(result, member->name.GetString(), Json(), allocator); }
        }
        if (result.MemberCount() == 0) { return false; }
        patch = std::move(result);
        return true;
    }
    if (base.IsArray() && value.IsArray() && isNamed(base) && isNamed(value) && !(base.Empty() && value.Empty())) {
        Json result(rapidjson::kObjectType);
        for (const Json &item : value.GetArray()) {
            const char *name = find(item, NAME)->GetString();
            const Json *baseItem = named(base, name);
            Json part;
            if (baseItem == nullptr) {
                part = copyOf(item, allocator);
                erase(part, NAME); // the key of the patch says it
            } else if (!diff(*baseItem, item, part, allocator)) {
                continue;
            }
            add(result, name, std::move(part), allocator);
        }
        for (const Json &item : base.GetArray()) {
            const char *name = find(item, NAME)->GetString();
            if (named(value, name) == nullptr) { add(result, name, Json(), allocator); }
        }
        if (result.MemberCount() == 0) { return false; }
        patch = std::move(result);
        return true;
    }
    if (base == value) { return false; }
    patch = copyOf(value, allocator);
    return true;
}

void merge(Json &target, const Json &patch, JsonAllocator &allocator) {
    if (patch.IsObject() && target.IsObject() && !isVector(target) && !isVector(patch)) {
        for (auto member = patch.MemberBegin(); member != patch.MemberEnd(); ++member) {
            const char *key = member->name.GetString();
            if (member->value.IsNull()) {
                erase(target, key);
            } else if (Json *own = find(target, key)) {
                merge(*own, member->value, allocator);
            } else {
                add(target, key, copyOf(member->value, allocator), allocator);
            }
        }
        return;
    }
    if (patch.IsObject() && target.IsArray() && isNamed(target) && !isVector(patch)) {
        for (auto member = patch.MemberBegin(); member != patch.MemberEnd(); ++member) {
            const char *name = member->name.GetString();
            Json *own = named(target, name);
            if (member->value.IsNull()) {
                if (own != nullptr) { target.Erase(own); }
            } else if (own != nullptr) {
                merge(*own, member->value, allocator);
            } else if (member->value.IsObject()) {
                Json item(rapidjson::kObjectType);
                add(item, NAME, Json(name, allocator), allocator);
                for (auto field = member->value.MemberBegin(); field != member->value.MemberEnd(); ++field) {
                    if (std::strcmp(field->name.GetString(), NAME) != 0) {
                        add(item, field->name.GetString(), copyOf(field->value, allocator), allocator);
                    }
                }
                target.PushBack(item, allocator);
            }
        }
        return;
    }
    target = copyOf(patch, allocator);
}

/* ------------------------------ The files ------------------------------ */

bool expand(const std::string &file, std::string &expanded) {
    Document document;
    if (!parse(file, document)) { return false; }
    Json *entities = find(document, ENTITIES);
    bool any = false;
    if (entities != nullptr && entities->IsArray()) {
        for (const Json &entry : entities->GetArray()) { any = any || find(entry, PREFAB) != nullptr; }
    }
    if (!any) { // nothing to expand: the file as it is
        expanded = file;
        return true;
    }
    JsonAllocator &allocator = document.GetAllocator();
    // The ids of the file are taken; the ids of the entities of an instance are derived around
    // them, the instances in the order of their ids.
    std::unordered_set<UUID> taken;
    std::vector<const Json *> instances;
    for (const Json &entry : entities->GetArray()) {
        taken.insert(idIn(entry, ID));
        if (find(entry, PREFAB) != nullptr) { instances.push_back(&entry); }
    }
    std::sort(instances.begin(), instances.end(), [](const Json *l, const Json *r) { return idIn(*l, ID) < idIn(*r, ID); });

    std::unordered_map<UUID, std::vector<ExpandedPart>> expandedOf; // by the id of the root
    std::unordered_map<UUID, Json> lone;                           // an instance of a prefab that is not there
    for (const Json *entry : instances) {
        const UUID instance = idIn(*entry, ID);
        const AssetId prefab = idIn(*entry, PREFAB);
        const Template *tmpl = templateOf(prefab);
        if (tmpl == nullptr) {
            if (isBeingRead(prefab)) {
                LOG_ERROR_EDITOR(
                    "A prefab holds an instance of itself (asset %u, entity %u): the instance is left out — take it out of the file.",
                    static_cast<uint32_t>(prefab), static_cast<uint32_t>(instance)
                );
            } else {
                LOG_ERROR_EDITOR(
                    "An instance (entity %u) is of a prefab the project does not have (asset %u): it is kept as it is until the prefab is back.",
                    static_cast<uint32_t>(instance), static_cast<uint32_t>(prefab)
                );
            }
            // One entity in its place, with the entry as it was read: it is written back as it is.
            Json entity(rapidjson::kObjectType);
            add(entity, ID, Json(static_cast<uint32_t>(instance)), allocator);
            const Json *relationship = find(*entry, RELATIONSHIP);
            add(entity, RELATIONSHIP, relationship != nullptr ? copyOf(*relationship, allocator) : Json(rapidjson::kObjectType), allocator);
            Json tag(rapidjson::kObjectType);
            add(tag, "tag", Json("Missing Prefab", allocator), allocator);
            add(entity, TAG, std::move(tag), allocator);
            Json link(rapidjson::kObjectType);
            add(link, "instance", Json(static_cast<uint32_t>(instance)), allocator);
            add(link, "source", Json(0u), allocator);
            add(link, "prefab", Json(static_cast<uint32_t>(prefab)), allocator);
            add(entity, PREFAB_LINK_KEY, std::move(link), allocator);
            add(entity, MISSING, copyOf(*entry, allocator), allocator);
            lone[instance] = std::move(entity);
            continue;
        }
        std::unordered_map<UUID, UUID> ids;
        for (UUID source : tmpl->ids) {
            if (source == tmpl->root) { continue; }
            const UUID id = freeDerivedEntityId(instance, source, [&taken](UUID id) { return taken.contains(id); });
            taken.insert(id);
            ids[source] = id;
        }
        expandEntry(*tmpl, *entry, [&ids](UUID source) { return ids.at(source); }, expandedOf[instance], allocator);
    }
    // The hierarchy: an entity of an instance lists what stands under it — entities of its
    // instance and entities of the file alike; an entity of the file lists the entities of
    // instances that were put under it too.
    std::unordered_map<UUID, std::vector<UUID>> under; // parent → the entities of instances under it
    std::unordered_set<UUID> partIds;
    for (auto &[instance, parts] : expandedOf) {
        for (const ExpandedPart &part : parts) {
            if (part.id != instance) {
                under[part.parent].push_back(part.id);
                partIds.insert(part.id);
            }
        }
    }
    std::unordered_map<UUID, std::vector<UUID>> listed; // parent → the entities of the file under it, as the parent fields say
    for (const Json &entry : entities->GetArray()) {
        const UUID parent = parentOf(entry);
        if (parent != UUID(0)) { listed[parent].push_back(idIn(entry, ID)); }
    }
    Json result(rapidjson::kArrayType);
    for (Json &entry : entities->GetArray()) {
        const UUID id = idIn(entry, ID);
        if (const auto alone = lone.find(id); alone != lone.end()) {
            result.PushBack(alone->second, allocator);
            continue;
        }
        const auto parts = expandedOf.find(id);
        if (parts == expandedOf.end()) {
            // An entity of the file: the entities of instances moved under it join its list.
            if (const auto more = under.find(id); more != under.end()) {
                Json *relationship = find(entry, RELATIONSHIP);
                if (relationship != nullptr && relationship->IsObject()) {
                    if (find(*relationship, CHILDREN) == nullptr) { add(*relationship, CHILDREN, Json(rapidjson::kArrayType), allocator); }
                    Json &children = *find(*relationship, CHILDREN);
                    for (UUID child : more->second) { children.PushBack(static_cast<uint32_t>(child), allocator); }
                }
            }
            result.PushBack(entry, allocator);
            continue;
        }
        for (ExpandedPart &part : parts->second) {
            Json relationship(rapidjson::kObjectType);
            add(relationship, PARENT, Json(static_cast<uint32_t>(part.parent)), allocator);
            Json children(rapidjson::kArrayType);
            std::unordered_set<UUID> seen;
            const auto push = [&](UUID child) {
                if (seen.insert(child).second) { children.PushBack(static_cast<uint32_t>(child), allocator); }
            };
            if (const auto more = under.find(part.id); more != under.end()) {
                for (UUID child : more->second) { push(child); }
            }
            if (const auto more = listed.find(part.id); more != listed.end()) {
                for (UUID child : more->second) {
                    if (!partIds.contains(child)) { push(child); }
                }
            }
            add(relationship, CHILDREN, std::move(children), allocator);
            add(part.object, RELATIONSHIP, std::move(relationship), allocator);
            result.PushBack(part.object, allocator);
        }
    }
    *entities = std::move(result);
    expanded = write(document, false);
    return true;
}

std::string collapse(const std::string &expanded) {
    Document document;
    if (!parse(expanded, document)) { return expanded; }
    Json *entities = find(document, ENTITIES);
    if (entities == nullptr || !entities->IsArray()) { return expanded; }
    JsonAllocator &allocator = document.GetAllocator();
    // The entities of instances, by the root of their instance and their id in the prefab.
    std::unordered_map<UUID, std::unordered_map<UUID, const Json *>> partsOf;
    std::unordered_set<UUID> implied; // the entities a file does not list: parts that are not roots
    std::unordered_set<UUID> roots;
    for (const Json &entity : entities->GetArray()) {
        const Json *link = find(entity, PREFAB_LINK_KEY);
        if (link == nullptr) { continue; }
        const UUID id = idIn(entity, ID);
        const UUID instance = idIn(*link, "instance");
        partsOf[instance][idIn(*link, "source")] = &entity;
        if (instance == id) { roots.insert(id); }
    }
    for (auto &[instance, parts] : partsOf) {
        if (!roots.contains(instance)) { continue; } // parts without their root are plain entities
        for (auto &[source, entity] : parts) {
            if (idIn(*entity, ID) != instance) { implied.insert(idIn(*entity, ID)); }
        }
    }
    const auto isImplied = [&implied](UUID id) { return implied.contains(id); };
    Json result(rapidjson::kArrayType);
    std::unordered_set<UUID> written; // the entities that stay entities of the file
    for (Json &entity : entities->GetArray()) {
        const UUID id = idIn(entity, ID);
        const Json *link = find(entity, PREFAB_LINK_KEY);
        if (link == nullptr) {
            result.PushBack(entity, allocator);
            continue;
        }
        const UUID instance = idIn(*link, "instance");
        if (instance != id) {
            if (implied.contains(id)) { continue; } // said by the entry of its instance
            erase(entity, PREFAB_LINK_KEY);         // its instance is gone: an entity like any other
            result.PushBack(entity, allocator);
            continue;
        }
        const Template *tmpl = templateOf(idIn(*link, "prefab"));
        if (tmpl == nullptr) {
            // The prefab is not there. The entry that was read is written back, at the place the
            // entity stands now; without one the entity stays an entity.
            if (Json *kept = find(entity, MISSING)) {
                Json entry = copyOf(*kept, allocator);
                if (const Json *relationship = find(entity, RELATIONSHIP)) { set(entry, RELATIONSHIP, copyOf(*relationship, allocator), allocator); }
                result.PushBack(entry, allocator);
            } else {
                erase(entity, PREFAB_LINK_KEY);
                for (auto &[source, part] : partsOf[instance]) { implied.erase(idIn(*part, ID)); }
                result.PushBack(entity, allocator);
            }
            continue;
        }
        result.PushBack(buildEntry(*tmpl, entity, partsOf[instance], isImplied, allocator), allocator);
    }
    // A part whose prefab turned out to be missing is an entity of the file after all.
    for (Json &entity : entities->GetArray()) {
        if (!entity.IsObject()) { continue; } // moved into the result
        const Json *link = find(entity, PREFAB_LINK_KEY);
        if (link != nullptr && !implied.contains(idIn(entity, ID)) && idIn(*link, "instance") != idIn(entity, ID)) {
            erase(entity, PREFAB_LINK_KEY);
            result.PushBack(entity, allocator);
        }
    }
    // An entity of the file lists the entities of the file under it and no others.
    for (Json &entity : result.GetArray()) {
        Json *relationship = find(entity, RELATIONSHIP);
        Json *children = relationship != nullptr ? find(*relationship, CHILDREN) : nullptr;
        if (children == nullptr || !children->IsArray()) { continue; }
        for (auto child = children->Begin(); child != children->End();) {
            child = implied.contains(idOf(*child)) ? children->Erase(child) : child + 1;
        }
    }
    *entities = std::move(result);
    return write(document, true);
}

bool load(World &world, const std::string &text) {
    std::string expanded;
    if (!expand(text, expanded)) { return false; }
    Rain::JsonDecoder decoder;
    std::istringstream stream(expanded);
    if (!WorldMapper::load(world, decoder, stream)) { return false; }
    resolveReferences(world);
    world.setChanged(false);
    return true;
}

std::string save(World &world, Entity root) {
    Rain::JsonEncoder encoder(true);
    std::ostringstream text;
    const bool instances = world.getAllEntitiesWith<PrefabComponent>().begin() != world.getAllEntitiesWith<PrefabComponent>().end();
    {
        // The entities of instances are written as a file names them, and put back as they were.
        std::unique_ptr<TemplateSpace> space = instances ? std::make_unique<TemplateSpace>(world) : nullptr;
        if (root.isValid()) {
            WorldMapper::saveTree(world, root, encoder, text);
        } else {
            WorldMapper::save(world, encoder, text);
        }
    }
    std::string file = instances ? collapse(text.str()) : text.str();
    // The root of a prefab file stands under nothing.
    if (root.isValid() && root.getParent().isValid()) {
        Document document;
        if (parse(file, document)) {
            if (Json *entities = find(document, ENTITIES); entities != nullptr && entities->IsArray()) {
                for (Json &entity : entities->GetArray()) {
                    Json *relationship = find(entity, RELATIONSHIP);
                    if (idIn(entity, ID) == root.getId() && relationship != nullptr) {
                        set(*relationship, PARENT, Json(0u), document.GetAllocator());
                    }
                }
            }
            file = write(document, true);
        }
    }
    return file;
}

void loadTemplate(AssetId prefab, const std::string &text, PrefabAsset &asset) {
    s_loading.push_back(prefab);
    if (!load(asset.world(), text)) {
        LOG_ERROR_EDITOR("The prefab (asset %u) is not valid JSON and was not loaded.", static_cast<uint32_t>(prefab));
        asset.world().clear();
    }
    s_loading.pop_back();
    const std::vector<UUID> roots = asset.world().orderedChildren(UUID(0));
    if (roots.size() > 1) {
        LOG_WARN_EDITOR(
            "The prefab (asset %u) has %zu entities without a parent: \"%s\" is its root, the others are not a part of it.",
            static_cast<uint32_t>(prefab), roots.size(), asset.root().getName().c_str()
        );
    }
    // Its scripts follow the loaded classes, like those of a world: an instance is compared
    // with it field by field.
    ScriptSystem::syncWithManifest(asset.world());
    asset.world().setChanged(false);
    s_templates.erase(prefab);
}

/* ------------------------------ An instance in a world ------------------------------ */

Entity instanceRoot(Entity entity) {
    if (!entity.isValid() || !entity.hasComponent<PrefabComponent>()) { return {}; }
    Entity root = entity.getWorld()->getById(entity.getComponent<PrefabComponent>().instance);
    return isRootOfInstance(root) ? root : Entity();
}

AssetId prefabOf(Entity entity) {
    Entity root = instanceRoot(entity);
    return root.isValid() ? root.getComponent<PrefabComponent>().prefab : AssetId(0);
}

std::string entryOf(Entity root) {
    Document entry;
    const Template *tmpl = nullptr;
    return readEntry(root, entry, tmpl) ? write(entry, true) : std::string();
}

void setEntry(Entity root, const std::string &text) {
    Document entry;
    if (!isRootOfInstance(root) || !parse(text, entry) || !entry.IsObject()) { return; }
    const Template *tmpl = templateOf(idIn(entry, PREFAB));
    if (tmpl == nullptr) { return; } // the prefab is gone: the entities stay as they are
    World &world = *root.getWorld();
    const UUID instance = root.getId();
    Parts parts = partsByInstance(world, instance)[instance];
    // What the entities are now, the references between them by the ids of the prefab.
    Document objects;
    std::unordered_map<UUID, Json> current;
    {
        TemplateSpace space(world, instance);
        for (auto &[source, entity] : parts.live) { current[source] = objectOf(entity, objects.GetAllocator()); }
    }
    // An entity that is in the world — live or under a tombstone — keeps its id; a new one gets
    // the derived one.
    const auto idFor = [&](UUID source) {
        if (const auto live = parts.live.find(source); live != parts.live.end()) { return live->second.getId(); }
        if (const auto buried = parts.buried.find(source); buried != parts.buried.end()) { return buried->second.getId(); }
        return freeDerivedEntityId(instance, source, [&world](UUID id) { return static_cast<entt::entity>(world.getById(id)) != entt::null; });
    };
    std::vector<ExpandedPart> expanded;
    expandEntry(*tmpl, entry, idFor, expanded, objects.GetAllocator());
    std::unordered_set<UUID> present;
    const Json none(rapidjson::kObjectType);
    for (const ExpandedPart &part : expanded) {
        present.insert(part.source);
        Entity entity;
        const Json *now = &none;
        if (const auto live = parts.live.find(part.source); live != parts.live.end()) {
            entity = live->second;
            now = &current.at(part.source);
        } else if (const auto buried = parts.buried.find(part.source); buried != parts.buried.end()) {
            entity = buried->second;
            entity.setDeleted(false); // it is back
            parts.live[part.source] = entity;
            Json object;
            {
                TemplateSpace space(world, instance);
                object = objectOf(entity, objects.GetAllocator());
            }
            now = &(current[part.source] = std::move(object));
        } else {
            entity = world.instantiateEntity(part.id);
            parts.live[part.source] = entity;
        }
        applyObject(entity, part.object, *now);
    }
    // The hierarchy, once every entity is there. The root stands where the world has it.
    for (const ExpandedPart &part : expanded) {
        if (part.source == tmpl->root) { continue; }
        Entity entity = parts.live.at(part.source);
        Entity parent = world.getById(part.parent);
        if (!(entity.getParent() == parent)) { world.setParent(entity, parent, /*keepWorldTransform*/ false); }
    }
    // What the entry has no more leaves the world, but not the registry: a command of the undo
    // history may hold it, and an entry that has it again finds it.
    for (auto &[source, entity] : parts.live) {
        if (!present.contains(source) && entity.isValid()) { entity.setDeleted(true); }
    }
    resolveReferences(world, instance);
    world.bumpStructureVersion();
}

const ComponentDifference *Difference::find(const std::string &component) const {
    for (const ComponentDifference &difference : components) {
        if (difference.component == component) { return &difference; }
    }
    return nullptr;
}

bool Difference::differs(const std::string &component, const std::string &field) const {
    const ComponentDifference *difference = find(component);
    if (difference == nullptr) { return false; }
    if (difference->kind != ComponentDifference::Kind::Changed || field.empty()) { return true; }
    for (const std::string &place : difference->fields) {
        // The place itself, something in it, or something it is in.
        if (place == field || place.rfind(field + "/", 0) == 0 || field.rfind(place + "/", 0) == 0) { return true; }
    }
    return false;
}

Difference differenceOf(Entity entity) {
    Difference difference;
    Entity root = instanceRoot(entity);
    if (!root.isValid()) { return difference; }
    const Template *tmpl = templateOf(root.getComponent<PrefabComponent>().prefab);
    const UUID source = entity.getComponent<PrefabComponent>().source;
    if (tmpl == nullptr || !tmpl->objects.contains(source)) { return difference; }
    difference.isPart = true;
    World &world = *entity.getWorld();
    const UUID instance = root.getId();
    Document document;
    Json object;
    {
        TemplateSpace space(world, instance);
        object = objectOf(entity, document.GetAllocator());
    }
    const Json &base = *tmpl->objects.at(source);
    Json patch(rapidjson::kObjectType);
    diffEntity(base, object, patch, document.GetAllocator());
    for (auto member = patch.MemberBegin(); member != patch.MemberEnd(); ++member) {
        ComponentDifference component;
        component.component = member->name.GetString();
        if (entity == root && component.component == TAG) { continue; } // what the root is called is its own
        const Json *own = find(base, member->name.GetString());
        if (member->value.IsNull()) {
            component.kind = ComponentDifference::Kind::Removed;
        } else if (own == nullptr) {
            component.kind = ComponentDifference::Kind::Added;
        } else {
            collectFields(*own, member->value, {}, component.fields);
            // Where the root of an instance stands is its own.
            if (entity == root && component.component == TRANSFORM) {
                std::erase_if(component.fields, [](const std::string &field) { return field == POSITION || field == ROTATION; });
                if (component.fields.empty()) { continue; }
            }
        }
        difference.components.push_back(std::move(component));
    }
    Parts parts = partsByInstance(world, instance)[instance];
    if (!(entity == root)) {
        const auto above = parts.live.find(tmpl->parents.at(source));
        Entity parent = entity.getParent();
        difference.moved = above == parts.live.end() || !(parent == above->second);
    } else {
        for (UUID id : tmpl->ids) {
            if (id == tmpl->root || parts.live.contains(id) || !parts.live.contains(tmpl->parents.at(id))) { continue; }
            difference.removed.push_back({id, tmpl->nameOf(id)});
        }
    }
    return difference;
}

size_t differenceCount(Entity root) {
    Document entry;
    const Template *tmpl = nullptr;
    if (!readEntry(root, entry, tmpl)) { return 0; }
    size_t count = 0;
    if (const Json *overrides = find(entry, OVERRIDES); overrides != nullptr && overrides->IsArray()) { count += overrides->Size(); }
    // The root counts when its entry says more of it than where it stands.
    return count + (rootDiffers(entry) ? 1 : 0);
}

std::string entryWithout(Entity root, const Scope &scope) {
    Document entry;
    const Template *tmpl = nullptr;
    if (!readEntry(root, entry, tmpl)) { return {}; }
    if (!scope.entity.isValid()) {
        // The whole instance: its prefab, at its place.
        eraseRootDifference(entry);
        erase(entry, OVERRIDES);
        return write(entry, true);
    }
    if (!scope.entity.hasComponent<PrefabComponent>()) { return write(entry, true); }
    const UUID source = scope.entity.getComponent<PrefabComponent>().source;
    if (scope.entity == root) {
        if (scope.component.empty()) {
            eraseRootDifference(entry);
        } else if (scope.component == TRANSFORM && scope.field.empty()) {
            Document bare;
            static_cast<Json &>(bare).CopyFrom(entry, bare.GetAllocator(), true);
            eraseRootDifference(bare);
            if (const Json *transform = find(bare, TRANSFORM)) { set(entry, TRANSFORM, copyOf(*transform, entry.GetAllocator()), entry.GetAllocator()); }
        } else if (scope.component != TAG && !(scope.component == TRANSFORM && (scope.field == POSITION || scope.field == ROTATION))) {
            erasePlace(entry, scope.component, scope.field);
        }
        return write(entry, true);
    }
    std::vector<UUID> path;
    if (!pathOf(*tmpl, source, path)) { return write(entry, true); }
    Json *item = overrideAt(entry, path);
    if (item == nullptr) { return write(entry, true); }
    if (scope.component.empty()) {
        std::vector<std::string> keys;
        for (auto member = item->MemberBegin(); member != item->MemberEnd(); ++member) {
            if (std::strcmp(member->name.GetString(), ENTITY) != 0) { keys.emplace_back(member->name.GetString()); }
        }
        for (const std::string &key : keys) { erase(*item, key.c_str()); }
    } else {
        erasePlace(*item, scope.component, scope.field);
        // Put back where the prefab has it: the move gave it a transform that kept its place
        // in the world, and that goes with the move.
        if (scope.component == PARENT) { erase(*item, TRANSFORM); }
    }
    prune(entry);
    return write(entry, true);
}

std::string entryRestoring(Entity root, UUID source) {
    Document entry;
    const Template *tmpl = nullptr;
    if (!readEntry(root, entry, tmpl)) { return {}; }
    std::vector<UUID> path;
    if (!pathOf(*tmpl, source, path)) { return write(entry, true); }
    if (Json *item = overrideAt(entry, path)) { erase(*item, REMOVED); }
    prune(entry);
    return write(entry, true);
}

Applied apply(Entity root, const Scope &scope, const std::string &file) {
    Applied applied;
    Document entry;
    const Template *tmpl = nullptr;
    if (!readEntry(root, entry, tmpl)) { return applied; }
    JsonAllocator &allocator = entry.GetAllocator();
    const bool whole = !scope.entity.isValid();
    // The part of the difference to apply, as patches of entities of the prefab.
    struct Chosen {
        UUID source = 0;
        Json patch;
        bool removed = false;
        UUID parent = 0; // the entity of the prefab it was moved under; 0 — it was not
    };
    std::vector<Chosen> chosen;
    World &world = *root.getWorld();
    Parts parts = partsByInstance(world, root.getId())[root.getId()];
    std::unordered_map<UUID, UUID> sources; // the id in the world → the id in the prefab
    for (auto &[source, entity] : parts.live) { sources[entity.getId()] = source; }
    const auto narrowed = [&](Json patch) {
        // One component, or one place of it: the rest of the patch stays with the instance.
        if (scope.component.empty()) { return patch; }
        Json only(rapidjson::kObjectType);
        Json *component = find(patch, scope.component.c_str());
        if (component == nullptr) { return only; }
        if (scope.field.empty()) {
            add(only, scope.component.c_str(), std::move(*component), allocator);
            return only;
        }
        const std::vector<std::string> keys = split(scope.field);
        Json *value = descend(*component, keys, keys.size());
        if (value == nullptr) { return only; }
        Json nested = std::move(*value);
        for (auto key = keys.rbegin(); key != keys.rend(); ++key) {
            Json wrap(rapidjson::kObjectType);
            add(wrap, key->c_str(), std::move(nested), allocator);
            nested = std::move(wrap);
        }
        add(only, scope.component.c_str(), std::move(nested), allocator);
        return only;
    };
    const UUID only = whole ? UUID(0) : scope.entity.hasComponent<PrefabComponent>() ? scope.entity.getComponent<PrefabComponent>().source : UUID(0);
    if (!whole && only == UUID(0)) { return applied; }
    if (whole || only == tmpl->root) {
        // The root: its difference is the entry itself; what it is called and where it stands
        // are not applied.
        Json patch(rapidjson::kObjectType);
        for (auto member = entry.MemberBegin(); member != entry.MemberEnd(); ++member) {
            const char *key = member->name.GetString();
            if (isOwnOfRoot(key)) { continue; }
            Json value = copyOf(member->value, allocator);
            if (std::strcmp(key, TRANSFORM) == 0) {
                erase(value, POSITION);
                erase(value, ROTATION);
                if (value.MemberCount() == 0) { continue; }
            }
            add(patch, key, std::move(value), allocator);
        }
        patch = narrowed(std::move(patch));
        if (patch.MemberCount() > 0) {
            Chosen item;
            item.source = tmpl->root;
            item.patch = std::move(patch);
            chosen.push_back(std::move(item));
        }
    }
    if (Json *overrides = find(entry, OVERRIDES); overrides != nullptr && overrides->IsArray()) {
        for (Json &item : overrides->GetArray()) {
            const UUID source = resolvePath(*tmpl, pathFrom(*find(item, ENTITY)));
            if (source == UUID(0) || (!whole && source != only)) { continue; }
            Chosen pick;
            pick.source = source;
            const Json *removed = find(item, REMOVED);
            pick.removed = whole && removed != nullptr && removed->IsBool() && removed->GetBool();
            if (const Json *parent = find(item, PARENT); whole && parent != nullptr) {
                const auto above = sources.find(idOf(*parent));
                pick.parent = above != sources.end() ? above->second : UUID(0); // moved out of the instance: stays its own
            }
            Json patch(rapidjson::kObjectType);
            for (auto member = item.MemberBegin(); member != item.MemberEnd(); ++member) {
                const char *key = member->name.GetString();
                if (std::strcmp(key, ENTITY) == 0 || std::strcmp(key, REMOVED) == 0 || std::strcmp(key, PARENT) == 0) { continue; }
                add(patch, key, copyOf(member->value, allocator), allocator);
            }
            pick.patch = narrowed(std::move(patch));
            if (pick.patch.MemberCount() > 0 || pick.removed || pick.parent != UUID(0)) { chosen.push_back(std::move(pick)); }
        }
    }
    // The plain entities added under the instance become entities of the prefab.
    std::vector<Entity> added;
    if (whole) {
        std::vector<Entity> pending;
        for (auto &[source, entity] : parts.live) { pending.push_back(entity); }
        while (!pending.empty()) {
            Entity above = pending.back();
            pending.pop_back();
            for (UUID childId : above.getChildEntities()) {
                Entity child = world.getById(childId);
                if (!child.isValid() || child.hasComponent<PrefabComponent>()) { continue; }
                added.push_back(child);
                pending.push_back(child);
            }
        }
    }
    if (chosen.empty() && added.empty()) { return applied; }

    // The prefab as a world of its own: what was chosen is put into its entities, then it is
    // written the way a prefab is saved.
    World prefab;
    if (!load(prefab, file)) { return applied; }
    Entity prefabRoot = prefab.getById(tmpl->root);
    if (!prefabRoot.isValid()) { return applied; }
    Document patches(rapidjson::kArrayType);
    for (Chosen &item : chosen) {
        Entity target = prefab.getById(item.source);
        if (!target.isValid()) { continue; }
        if (item.removed) {
            prefab.destroy(target);
            continue;
        }
        if (item.patch.MemberCount() > 0) {
            Document objects;
            const Json now = objectOf(target, objects.GetAllocator());
            Json wanted = copyOf(now, objects.GetAllocator());
            merge(wanted, item.patch, objects.GetAllocator());
            applyObject(target, wanted, now);
            notePatch(patches, *tmpl, item.source, item.patch, patches.GetAllocator());
        }
        if (item.parent != UUID(0)) {
            Entity above = prefab.getById(item.parent);
            if (above.isValid()) { prefab.setParent(target, above, /*keepWorldTransform*/ false); }
        }
    }
    // Added entities: the same ids where the prefab has them free.
    std::unordered_map<UUID, UUID> addedIds; // the id in the world → the id in the prefab
    {
        TemplateSpace space(world, root.getId());
        for (Entity entity : added) {
            UUID id = entity.getId();
            if (static_cast<entt::entity>(prefab.getById(id)) != entt::null) { id = prefab.newEntityId(); }
            addedIds[entity.getId()] = id;
            Entity made = prefab.instantiateEntity(id);
            Document objects;
            Json object = objectOf(entity, objects.GetAllocator());
            erase(object, ID);
            erase(object, RELATIONSHIP);
            Rain::JsonDecoder decoder;
            std::istringstream stream(write(object, false));
            WorldMapper::loadEntity(made, decoder, stream);
        }
    }
    for (Entity entity : added) {
        const UUID parent = entity.getParent().getId();
        const auto part = sources.find(parent);
        const UUID above = part != sources.end() ? part->second : addedIds.at(parent);
        prefab.setParent(prefab.getById(addedIds.at(entity.getId())), prefab.getById(above), /*keepWorldTransform*/ false);
    }
    // In the world they are parts of the instance from now on, with the ids of parts.
    for (Entity entity : added) {
        const UUID source = addedIds.at(entity.getId());
        const UUID id = freeDerivedEntityId(root.getId(), source, [&world](UUID id) { return static_cast<entt::entity>(world.getById(id)) != entt::null; });
        world.changeEntityId(entity, id);
        PrefabComponent &link = entity.addComponent<PrefabComponent>();
        link.instance = root.getId();
        link.source = source;
    }
    applied.prefab = tmpl->id;
    applied.instance = root.getId();
    applied.file = save(prefab, prefabRoot);
    applied.patches = write(patches, false);
    return applied;
}

bool stripApplied(const Applied &applied, const std::string &file, std::string &result) {
    Document document;
    Document patches;
    if (!parse(file, document) || !parse(applied.patches, patches) || !patches.IsArray()) { return false; }
    Json *entities = find(document, ENTITIES);
    if (entities == nullptr || !entities->IsArray()) { return false; }
    bool changed = false;
    for (Json &entry : entities->GetArray()) {
        if (find(entry, PREFAB) == nullptr || idIn(entry, ID) == applied.instance) { continue; }
        changed = stripEntry(entry, patches, applied.prefab) || changed;
    }
    if (changed) { result = write(document, true); }
    return changed;
}

std::string createFrom(Entity root) {
    return root.isValid() ? save(*root.getWorld(), root) : std::string();
}

void convertToInstance(Entity root, AssetId prefab) {
    if (!root.isValid()) { return; }
    World &world = *root.getWorld();
    const UUID instance = root.getId();
    // The entities under the root, by the ids they had: those are their ids in the prefab.
    std::vector<Entity> under;
    std::vector<Entity> pending{root};
    while (!pending.empty()) {
        Entity above = pending.back();
        pending.pop_back();
        for (UUID childId : above.getChildEntities()) {
            Entity child = world.getById(childId);
            if (!child.isValid()) { continue; }
            under.push_back(child);
            pending.push_back(child);
        }
    }
    std::sort(under.begin(), under.end(), [](const Entity &l, const Entity &r) { return l.getId() < r.getId(); });
    const auto mark = [&](Entity entity, UUID source, bool isRoot) {
        // The root of an instance of another prefab that stood in the tree is nested in this one.
        const bool nestedRoot = !isRoot && isRootOfInstance(entity);
        const AssetId nested = nestedRoot ? entity.getComponent<PrefabComponent>().prefab : AssetId(0);
        PrefabComponent &link = entity.hasComponent<PrefabComponent>() ? entity.getComponent<PrefabComponent>() : entity.addComponent<PrefabComponent>();
        link.instance = instance;
        link.source = source;
        link.prefab = isRoot ? prefab : nested;
    };
    // The root first: an instance that was the root keeps the prefab it is of in the file, and
    // is the root of this one here.
    mark(root, instance, true);
    for (Entity entity : under) {
        const UUID source = entity.getId();
        mark(entity, source, false);
        const UUID id = freeDerivedEntityId(instance, source, [&world](UUID id) { return static_cast<entt::entity>(world.getById(id)) != entt::null; });
        world.changeEntityId(entity, id);
    }
    world.bumpStructureVersion();
    world.setChanged();
}

void unpack(Entity root) {
    if (!isRootOfInstance(root)) { return; }
    World &world = *root.getWorld();
    const UUID instance = root.getId();
    const Template *tmpl = templateOf(root.getComponent<PrefabComponent>().prefab);
    Parts parts = partsByInstance(world, instance)[instance];
    // The entities of an instance of another prefab nested in this one: where each came from there.
    const auto nestedLink = [tmpl](UUID source) -> const Template::Link * {
        if (tmpl == nullptr) { return nullptr; }
        const auto link = tmpl->links.find(source);
        return link != tmpl->links.end() ? &link->second : nullptr;
    };
    // Such an instance stands on its own from now on: its root is the root of an instance, and
    // its entities get the ids derived from that root. Everything else is a plain entity.
    std::unordered_map<UUID, Entity> nestedRoots; // by the id of the nested instance in the prefab
    for (auto &[source, entity] : parts.live) {
        const Template::Link *link = nestedLink(source);
        if (link != nullptr && link->instance == source) { nestedRoots[source] = entity; }
    }
    for (auto &[source, entity] : parts.live) {
        const Template::Link *link = nestedLink(source);
        if (link == nullptr || !nestedRoots.contains(link->instance)) {
            entity.removeComponent<PrefabComponent>();
            continue;
        }
        Entity nestedRoot = nestedRoots.at(link->instance);
        PrefabComponent &own = entity.getComponent<PrefabComponent>();
        own.instance = nestedRoot.getId();
        own.source = link->source;
        own.prefab = entity == nestedRoot ? link->prefab : AssetId(0);
    }
    for (auto &[source, entity] : parts.live) {
        if (!entity.hasComponent<PrefabComponent>()) { continue; }
        const PrefabComponent own = entity.getComponent<PrefabComponent>();
        if (own.instance == entity.getId() || derivedEntityId(own.instance, own.source) == entity.getId()) { continue; }
        world.changeEntityId(
            entity,
            freeDerivedEntityId(own.instance, own.source, [&world](UUID id) { return static_cast<entt::entity>(world.getById(id)) != entt::null; })
        );
    }
    // The parts under a tombstone are parts of nothing: they come back, if ever, as plain entities.
    for (auto &[source, entity] : parts.buried) { entity.removeComponent<PrefabComponent>(); }
    world.bumpStructureVersion();
    world.setChanged();
}

/* ------------------------------ What is open ------------------------------ */

bool dependsOn(AssetId prefab, AssetId other) {
    if (prefab == other) { return true; }
    const Template *tmpl = templateOf(prefab);
    if (tmpl == nullptr) { return false; }
    for (AssetId nested : tmpl->nested) {
        if (nested != prefab && dependsOn(nested, other)) { return true; }
    }
    return false;
}

std::vector<AssetId> prefabsIn(World &world) {
    std::set<AssetId> prefabs;
    auto view = world.getAllEntitiesWith<PrefabComponent>();
    for (auto handle : view) {
        const PrefabComponent &link = view.get<PrefabComponent>(handle);
        if (link.prefab != AssetId(0) && world.isValidEntt(handle)) { prefabs.insert(link.prefab); }
    }
    return std::vector<AssetId>(prefabs.begin(), prefabs.end());
}

void prefabChanged(
    AssetId prefab, const std::vector<World *> &worlds, const std::function<void(AssetId)> &reload, const Applied *forced
) {
    // What follows the prefab: itself and every prefab that holds it. Their instances are read
    // against the prefabs as they were.
    std::unordered_set<AssetId> affected{prefab};
    for (auto &[id, tmpl] : s_templates) {
        if (dependsOn(id, prefab)) { affected.insert(id); }
    }
    struct Kept {
        World *world = nullptr;
        UUID root = 0;
        std::string entry;
    };
    std::vector<Kept> kept;
    // The dirty flag a world has afterwards: what it had, or set — an instance lost keys to an
    // Apply Forced, and its file holds another entry now.
    std::unordered_map<World *, bool> dirty;
    Document patches;
    if (forced != nullptr) { parse(forced->patches, patches); }
    for (World *world : worlds) {
        dirty[world] = world->isChanged();
        auto view = world->getAllEntitiesWith<PrefabComponent>();
        std::vector<Entity> roots;
        for (auto handle : view) {
            const PrefabComponent &link = view.get<PrefabComponent>(handle);
            Entity entity = world->entity(handle);
            if (world->isValidEntt(handle) && link.instance == entity.getId() && affected.contains(link.prefab)) { roots.push_back(entity); }
        }
        for (Entity root : roots) {
            Document entry;
            const Template *tmpl = nullptr;
            if (!readEntry(root, entry, tmpl)) { continue; }
            // Apply Forced: the other instances take the applied values. The one the difference
            // was taken from has nothing to save for it either way: its file holds what the
            // prefab now has.
            if (forced != nullptr && patches.IsArray()) {
                if (root.getId() == forced->instance || stripEntry(entry, patches, forced->prefab)) { dirty[world] = true; }
            }
            kept.push_back({world, root.getId(), write(entry, false)});
        }
    }
    // The prefabs are read again as they are asked for — one that holds another asks for it
    // while it is read, so each is read after what it holds.
    for (AssetId id : affected) { s_stale.insert(id); }
    s_reload = &reload;
    for (AssetId id : affected) { templateOf(id); }
    for (const Kept &instance : kept) {
        Entity root = instance.world->getById(instance.root);
        if (root.isValid()) { setEntry(root, instance.entry); }
    }
    // The file of a world holds the same entries as before: nothing to save, unless said above.
    for (auto &[world, changed] : dirty) { world->setChanged(changed); }
    s_reload = nullptr;
    s_stale.clear();
}

void scriptsReloaded() {
    for (auto &[id, tmpl] : s_templates) {
        if (PrefabAsset *asset = tmpl->asset.get()) { ScriptSystem::syncWithManifest(asset->world()); }
    }
    s_templates.clear(); // the objects the instances are compared with are made again
}

void clear() {
    s_templates.clear();
    s_stale.clear();
}

} // namespace Panda::Prefabs

#endif // PND_EDITOR
