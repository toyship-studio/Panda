// Prefabs (Prefabs.hpp): an instance in a world — what it differs in, Revert, Restore, Put Back,
// Apply and Apply Forced, making and unpacking one.

#include "Panda/Prefabs/Prefabs.hpp"

#ifdef PND_EDITOR

#    include "Panda/Prefabs/PrefabsInternal.hpp"

namespace Panda::Prefabs::Detail {

namespace {

    // The entry says something of the root besides what it is called and where it stands.
    bool rootDiffers(const Json &entry) {
        for (auto member = entry.members().begin(); member != entry.members().end(); ++member) {
            const char *key = member->key.c_str();
            if (isOwnOfRoot(key)) { continue; }
            if (std::strcmp(key, TRANSFORM) != 0) { return true; }
            for (auto inner = member->value.members().begin(); inner != member->value.members().end(); ++inner) {
                const char *name = inner->key.c_str();
                if (std::strcmp(name, POSITION) != 0 && std::strcmp(name, ROTATION) != 0) { return true; }
            }
        }
        return false;
    }

    // The places of a component that differ, as paths of keys: the patch is followed down while
    // the prefab has an object (or a list by name) there.
    void collectFields(const Json &base, const Json &patch, const std::string &prefix, std::vector<std::string> &fields) {
        const bool object = base.isObject() && !isVector(base);
        const bool list = base.isArray() && isNamed(base);
        if (!patch.isObject() || isVector(patch) || (!object && !list)) {
            fields.push_back(prefix);
            return;
        }
        for (auto member = patch.members().begin(); member != patch.members().end(); ++member) {
            const char *key = member->key.c_str();
            const Json *inner = object ? base.find(key) : named(base, key);
            const std::string path = prefix.empty() ? key : prefix + "/" + key;
            if (inner == nullptr || member->value.isNull()) {
                fields.push_back(path);
            } else {
                collectFields(*inner, member->value, path, fields);
            }
        }
    }

    /* ------------------------------ Places of a difference ------------------------------ */
    // Revert in all its sizes, Restore, Put Back and the other instances of an Apply Forced are
    // one operation: places are taken out of the difference an entry holds (`strip`). A place is
    // an entity of a prefab, named by its path there, and a mask of what of it:
    //     {"entity": 78, "pointLightComponent": {"intensity": true}}  — one field
    //     {"entity": 78, "pointLightComponent": true}                 — a component
    //     {"entity": 78, "*": true}                                   — all the entity differs in
    //     {"entity": 79, "removed": true}, {"entity": 80, "parent": true, "transformComponent": true}
    // A patch serves as its own mask: the places an Apply wrote are the patches it wrote.

    constexpr const char *EVERYTHING = "*";

    // A place of an entity of the prefab of `tmpl`, without a mask yet.
    Json &addPlace(Json &places, const std::vector<UUID> &path) {
        Json place = Json::object();
        place.add(ENTITY, pathJson(path));
        return places.push(std::move(place));
    }

    // A place with everything the entity says in it.
    void notePlace(Json &places, const Template &tmpl, UUID source, const Json &mask) {
        std::vector<UUID> path;
        if (!pathOf(tmpl, source, path)) { return; }
        Json &place = addPlace(places, path);
        for (auto member = mask.members().begin(); member != mask.members().end(); ++member) {
            place.add(member->key.c_str(), Json(member->value));
        }
    }

    // The places a mask names, taken out of a patch — an item of the overrides, or the entry
    // itself (`root`: it is the patch of the root of its instance). A key of the mask with an
    // object goes deeper, any other value takes the key with everything in it, `"*"` takes every
    // key; a key left empty on the way goes too. What the root has of its own — its name and its
    // place — stays whatever the mask says. A deeper place inside what the patch replaces as a
    // whole (a field of a component the instance took off) leaves the patch as it is: the
    // instance differs there by more than that place.
    void subtract(Json &patch, const Json &mask, bool root) {
        if (!patch.isObject() || !mask.isObject() || isVector(patch) || isVector(mask)) { return; }
        const bool everything = mask.find(EVERYTHING) != nullptr;
        std::vector<std::string> gone;
        for (auto member = patch.members().begin(); member != patch.members().end(); ++member) {
            const char *key = member->key.c_str();
            if (std::strcmp(key, ENTITY) == 0 || (root && isOwnOfRoot(key))) { continue; }
            const Json *inner = everything ? nullptr : mask.find(key);
            if (!everything && inner == nullptr) { continue; }
            Json &own = member->value;
            const bool deeper = inner != nullptr && inner->isObject() && !isVector(*inner) && inner->find(EVERYTHING) == nullptr;
            if (root && std::strcmp(key, TRANSFORM) == 0 && own.isObject()) {
                // Of the transform of a root, what is not its place.
                std::vector<std::string> inside;
                for (auto field = own.members().begin(); field != own.members().end(); ++field) {
                    const char *name = field->key.c_str();
                    if (std::strcmp(name, POSITION) == 0 || std::strcmp(name, ROTATION) == 0) { continue; }
                    if (!deeper || inner->find(name) != nullptr) { inside.emplace_back(name); }
                }
                for (const std::string &name : inside) { own.erase(name.c_str()); }
                if (own.size() == 0) { gone.emplace_back(key); }
                continue;
            }
            if (!deeper) {
                gone.emplace_back(key);
            } else if (own.isObject() && !isVector(own)) {
                subtract(own, *inner, false);
                if (own.size() == 0) { gone.emplace_back(key); }
            }
        }
        for (const std::string &key : gone) { patch.erase(key.c_str()); }
    }

    // The entity a path names in the template `tmpl` is the entity `target` names in the
    // template of `prefab`: the same prefab and the same path — or the same entity of `prefab`
    // reached through the instances nested on the way, or through the prefab a root is a variant
    // of (its root is an instance of that one).
    bool correspondsTo(const Template *tmpl, const std::vector<UUID> &path, AssetId prefab, const std::vector<UUID> &target) {
        for (size_t depth = 0; tmpl != nullptr && depth < path.size(); depth++) {
            if (tmpl->id == prefab && std::equal(path.begin() + static_cast<std::ptrdiff_t>(depth), path.end(), target.begin(), target.end())) {
                return true;
            }
            if (depth + 1 == path.size()) {
                // The last step names an entity of this template: the root of an instance nested
                // here (or the root of the template, a variant) is the root of its prefab, and of
                // what that prefab is a variant of in turn.
                const Template *at = tmpl;
                UUID entity = path[depth];
                for (size_t guard = 0; guard < 64; guard++) {
                    const auto link = at->links.find(entity);
                    if (link == at->links.end() || link->second.instance != entity || link->second.prefab == AssetId(0)) { return false; }
                    at = templateOf(link->second.prefab);
                    if (at == nullptr) { return false; }
                    entity = at->root;
                    if (at->id == prefab && target.size() == 1 && target.front() == entity) { return true; }
                }
                return false;
            }
            const auto link = tmpl->links.find(path[depth]);
            tmpl = link != tmpl->links.end() ? templateOf(link->second.prefab) : nullptr;
        }
        return false;
    }

    // The places a scope names in the entry of its instance.
    void placesOf(const Template &tmpl, const Json &entry, const Scope &scope, Json &places) {
        if (!scope.entity.isValid()) {
            // The whole instance: the root, and every entity the entry says something of.
            addPlace(places, {tmpl.root}).add(EVERYTHING, Json(true));
            if (const Json *list = entry.find(OVERRIDES); list != nullptr && list->isArray()) {
                for (const Json &item : list->items()) {
                    if (const Json *named = item.find(ENTITY)) { addPlace(places, pathFrom(*named)).add(EVERYTHING, Json(true)); }
                }
            }
            return;
        }
        if (!scope.entity.hasComponent<PrefabComponent>()) { return; }
        std::vector<UUID> path;
        if (!pathOf(tmpl, scope.entity.getComponent<PrefabComponent>().source, path)) { return; }
        Json &place = addPlace(places, path);
        if (scope.component.empty()) {
            place.add(EVERYTHING, Json(true));
        } else if (scope.component == PARENT) {
            // Put Back: under the entity the prefab says, as the prefab has it there — the move gave
            // it a transform that kept its place in the world, and that goes with the move.
            place.add(PARENT, Json(true));
            place.add(TRANSFORM, Json(true));
        } else {
            Json mask(true);
            const std::vector<std::string> keys = split(scope.field);
            for (auto key = keys.rbegin(); key != keys.rend(); ++key) {
                Json wrap = Json::object();
                wrap.add(key->c_str(), std::move(mask));
                mask = std::move(wrap);
            }
            place.add(scope.component.c_str(), std::move(mask));
        }
    }

    /* ------------------------------ Additions ------------------------------ */

    // The topmost entities added to an instance: the children of its entities that are not its
    // parts, in the order of their ids.
    std::vector<Entity> additionsTo(World &world, const Parts &parts, UUID instance) {
        std::vector<Entity> found;
        for (auto [source, entity] : parts.live) {
            for (UUID childId : entity.getChildEntities()) {
                Entity child = world.getById(childId);
                if (!child.isValid()) { continue; }
                if (child.hasComponent<PrefabComponent>() && child.getComponent<PrefabComponent>().instance == instance) { continue; }
                found.push_back(child);
            }
        }
        std::sort(found.begin(), found.end(), [](const Entity &l, const Entity &r) { return l.getId() < r.getId(); });
        return found;
    }

} // namespace

bool strip(Json &entry, const Json &places, AssetId prefab) {
    const Template *tmpl = templateOf(idIn(entry, PREFAB));
    if (tmpl == nullptr || !places.isArray()) { return false; }
    const std::string before = Rain::writeJson(entry);
    Json *list = entry.find(OVERRIDES);
    for (const Json &place : places.items()) {
        const Json *named = place.find(ENTITY);
        if (named == nullptr) { continue; }
        const std::vector<UUID> target = pathFrom(*named);
        // The root of the instance: what it differs in is the entry itself.
        if (correspondsTo(tmpl, {tmpl->root}, prefab, target)) { subtract(entry, place, true); }
        if (list == nullptr || !list->isArray()) { continue; }
        for (Json &item : list->items()) {
            const Json *path = item.find(ENTITY);
            if (path != nullptr && correspondsTo(tmpl, pathFrom(*path), prefab, target)) { subtract(item, place, false); }
        }
    }
    prune(entry);
    return Rain::writeJson(entry) != before;
}

} // namespace Panda::Prefabs::Detail

namespace Panda::Prefabs {

using namespace Detail;

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

Addition additionOf(Entity entity) {
    // Up from the entity: the first one on the way that stands under an entity of an instance
    // without being a part of that instance is what was added to it.
    for (Entity at = entity; at.isValid();) {
        Entity parent = at.getParent();
        if (!parent.isValid()) { break; }
        Entity owner = instanceRoot(parent);
        if (owner.isValid()) {
            const bool part = at.hasComponent<PrefabComponent>() && at.getComponent<PrefabComponent>().instance == owner.getId();
            if (!part) { return {owner, at}; }
        }
        at = parent;
    }
    return {};
}

std::string entryOf(Entity root) {
    Json entry;
    const Template *tmpl = nullptr;
    return readEntry(root, entry, tmpl) ? Rain::writeJson(entry, Rain::JsonLayout::Pretty) : std::string();
}

void setEntry(Entity root, const std::string &text) {
    Json entry;
    if (!isRootOfInstance(root) || !Rain::parseJson(text, entry) || !entry.isObject()) { return; }
    const Template *tmpl = templateOf(idIn(entry, PREFAB));
    if (tmpl == nullptr) { return; } // the prefab is gone: the entities stay as they are
    World &world = *root.getWorld();
    const UUID instance = root.getId();
    Parts parts = partsByInstance(world, instance)[instance];
    // What the entities are now, the references between them by the ids of the prefab.
    Json objects;
    std::unordered_map<UUID, Json> current;
    {
        TemplateSpace space(world, instance);
        for (auto &[source, entity] : parts.live) { current[source] = objectOf(entity); }
    }
    // An entity that is in the world — live or under a tombstone — keeps its id; a new one gets
    // the derived one.
    const auto idFor = [&](UUID source) {
        if (const auto live = parts.live.find(source); live != parts.live.end()) { return live->second.getId(); }
        if (const auto buried = parts.buried.find(source); buried != parts.buried.end()) { return buried->second.getId(); }
        return freeDerivedEntityId(instance, source, [&world](UUID id) { return static_cast<entt::entity>(world.getById(id)) != entt::null; });
    };
    std::vector<ExpandedPart> expanded;
    expandEntry(*tmpl, entry, idFor, expanded);
    std::unordered_set<UUID> present;
    const Json none = Json::object();
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
                object = objectOf(entity);
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
    Json document;
    Json object;
    {
        TemplateSpace space(world, instance);
        object = objectOf(entity);
    }
    const Json &base = *tmpl->objects.at(source);
    Json patch = Json::object();
    diffEntity(base, object, patch);
    for (auto member = patch.members().begin(); member != patch.members().end(); ++member) {
        ComponentDifference component;
        component.component = member->key.c_str();
        if (entity == root && component.component == TAG) { continue; } // what the root is called is its own
        const Json *own = base.find(member->key.c_str());
        if (member->value.isNull()) {
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
        for (Entity added : additionsTo(world, parts, instance)) { difference.added.push_back({added.getId(), added.getName()}); }
    }
    return difference;
}

size_t differenceCount(Entity root) {
    Json entry;
    const Template *tmpl = nullptr;
    if (!readEntry(root, entry, tmpl)) { return 0; }
    size_t count = 0;
    if (const Json *overrides = entry.find(OVERRIDES); overrides != nullptr && overrides->isArray()) { count += overrides->size(); }
    // The root counts when its entry says more of it than what it is called and where it stands;
    // every addition counts as an entity of its own.
    World &world = *root.getWorld();
    const Parts parts = partsByInstance(world, root.getId())[root.getId()];
    return count + (rootDiffers(entry) ? 1 : 0) + additionsTo(world, parts, root.getId()).size();
}

std::string entryWithout(Entity root, const Scope &scope) {
    Json entry;
    const Template *tmpl = nullptr;
    if (!readEntry(root, entry, tmpl)) { return {}; }
    Json places = Json::array();
    placesOf(*tmpl, entry, scope, places);
    strip(entry, places, tmpl->id);
    return Rain::writeJson(entry, Rain::JsonLayout::Pretty);
}

std::string entryRestoring(Entity root, UUID source) {
    Json entry;
    const Template *tmpl = nullptr;
    if (!readEntry(root, entry, tmpl)) { return {}; }
    std::vector<UUID> path;
    if (pathOf(*tmpl, source, path)) {
        Json places = Json::array();
        addPlace(places, path).add(REMOVED, Json(true));
        strip(entry, places, tmpl->id);
    }
    return Rain::writeJson(entry, Rain::JsonLayout::Pretty);
}

Applied apply(Entity root, const Scope &scope, const std::string &file) {
    Applied applied;
    Json entry;
    const Template *tmpl = nullptr;
    if (!readEntry(root, entry, tmpl)) { return applied; }
    World &world = *root.getWorld();
    const UUID instance = root.getId();
    Parts parts = partsByInstance(world, instance)[instance];
    std::unordered_map<UUID, UUID> sources; // the id in the world → the id in the prefab
    for (auto &[source, entity] : parts.live) { sources[entity.getId()] = source; }
    const bool whole = !scope.entity.isValid();
    // An addition goes into the prefab as it is; anything else of the instance by its patches.
    const Addition addition = whole ? Addition{} : additionOf(scope.entity);
    const bool adding = addition.instance.isValid() && addition.instance == root;

    // The part of the difference to apply, as patches of entities of the prefab.
    struct Chosen {
        UUID source = 0;
        Json patch;
        bool removed = false;
        UUID parent = 0; // the entity of the world it was moved under; 0 — it was not
    };
    std::vector<Chosen> chosen;
    const auto narrowed = [&](Json patch) {
        // One component, or one place of it: the rest of the patch stays with the instance.
        if (scope.component.empty()) { return patch; }
        Json only = Json::object();
        Json *component = patch.find(scope.component.c_str());
        if (component == nullptr) { return only; }
        if (scope.field.empty()) {
            only.add(scope.component.c_str(), std::move(*component));
            return only;
        }
        const std::vector<std::string> keys = split(scope.field);
        Json *value = descend(*component, keys, keys.size());
        if (value == nullptr) { return only; }
        Json nested = std::move(*value);
        for (auto key = keys.rbegin(); key != keys.rend(); ++key) {
            Json wrap = Json::object();
            wrap.add(key->c_str(), std::move(nested));
            nested = std::move(wrap);
        }
        only.add(scope.component.c_str(), std::move(nested));
        return only;
    };
    if (!adding) {
        const UUID only = whole ? UUID(0) : scope.entity.hasComponent<PrefabComponent>() ? scope.entity.getComponent<PrefabComponent>().source : UUID(0);
        if (!whole && only == UUID(0)) { return applied; }
        if (whole || only == tmpl->root) {
            // The root: its difference is the entry itself; what it is called and where it stands
            // are not applied.
            Json patch = Json::object();
            for (auto member = entry.members().begin(); member != entry.members().end(); ++member) {
                const char *key = member->key.c_str();
                if (isOwnOfRoot(key)) { continue; }
                Json value = Json(member->value);
                if (std::strcmp(key, TRANSFORM) == 0) {
                    value.erase(POSITION);
                    value.erase(ROTATION);
                    if (value.size() == 0) { continue; }
                }
                patch.add(key, std::move(value));
            }
            patch = narrowed(std::move(patch));
            if (patch.size() > 0) {
                Chosen item;
                item.source = tmpl->root;
                item.patch = std::move(patch);
                chosen.push_back(std::move(item));
            }
        }
        // A move goes with the whole instance, with the entity as a whole, or by itself.
        const bool moves = whole || scope.component.empty() || scope.component == PARENT;
        if (Json *overrides = entry.find(OVERRIDES); overrides != nullptr && overrides->isArray()) {
            for (Json &item : overrides->items()) {
                const UUID source = resolvePath(*tmpl, pathFrom(*item.find(ENTITY)));
                if (source == UUID(0) || (!whole && source != only)) { continue; }
                Chosen pick;
                pick.source = source;
                const Json *removed = item.find(REMOVED);
                pick.removed = whole && removed != nullptr && removed->isBool() && removed->getBool();
                if (const Json *parent = item.find(PARENT); moves && parent != nullptr) { pick.parent = idOf(*parent); }
                Json patch = Json::object();
                for (auto member = item.members().begin(); member != item.members().end(); ++member) {
                    const char *key = member->key.c_str();
                    if (std::strcmp(key, ENTITY) == 0 || std::strcmp(key, REMOVED) == 0 || std::strcmp(key, PARENT) == 0) { continue; }
                    patch.add(key, Json(member->value));
                }
                pick.patch = scope.component == PARENT ? Json::object() : narrowed(std::move(patch));
                if (pick.patch.size() > 0 || pick.removed || pick.parent != UUID(0)) { chosen.push_back(std::move(pick)); }
            }
        }
    }
    // What was added to the instance: with the whole of it, every addition; or the one named.
    std::vector<Entity> additions;
    if (whole) {
        additions = additionsTo(world, parts, instance);
    } else if (adding) {
        additions.push_back(addition.top);
    }
    if (chosen.empty() && additions.empty()) { return applied; }
    // A prefab cannot hold itself: an addition that is — or holds — an instance of this prefab,
    // or of one that holds it, stays the instance's own.
    for (Entity top : additions) {
        for (Entity at : world.subtree(top)) {
            const AssetId nested = at.hasComponent<PrefabComponent>() ? at.getComponent<PrefabComponent>().prefab : AssetId(0);
            if (nested != AssetId(0) && dependsOn(nested, tmpl->id)) {
                LOG_WARN_EDITOR(
                    "%s is an instance of a prefab that holds %s: applied into it, the prefab would hold itself. Nothing was applied.",
                    at.getName().c_str(), tmpl->nameOf(tmpl->root).c_str()
                );
                applied.refused = true;
                return applied;
            }
        }
    }

    // The prefab as a world of its own: what was chosen is put into its entities, then it is
    // written the way a prefab is saved.
    World prefab;
    if (!load(prefab, file)) { return applied; }
    Entity prefabRoot = prefab.getById(tmpl->root);
    if (!prefabRoot.isValid()) { return applied; }
    Json places = Json::array();

    // The additions: copies of what was added, under the entities of the prefab they stand
    // under, with the transforms they have there. An instance of another prefab stays one: the
    // prefab holds it nested from now on.
    std::unordered_map<UUID, UUID> adopted; // the id in the world → the id of its copy in the prefab
    for (Entity top : additions) {
        const auto above = sources.find(top.getParent().getId());
        Entity parent = above != sources.end() ? prefab.getById(above->second) : Entity();
        if (!parent.isValid()) { continue; }
        std::unordered_map<UUID, UUID> ids;
        EntityPlacement placement;
        placement.parent = parent;
        // A part of the instance moved under an addition is not copied with it: it is moved there
        // in the prefab, as it is in the instance.
        const auto isPart = [instance](Entity entity) {
            return entity.hasComponent<PrefabComponent>() && entity.getComponent<PrefabComponent>().instance == instance;
        };
        prefab.importEntity(world, top, placement, &ids, isPart);
        adopted.insert(ids.begin(), ids.end());
    }
    // What the copies name of the instance, they name by the ids of the prefab.
    const ComponentTypeRegistry &types = GameContext::getExtensions().components;
    for (auto &[original, copy] : adopted) {
        types.forEachReference(prefab.getById(copy), [&sources](UUID &reference) {
            const auto found = sources.find(reference);
            if (found != sources.end()) { reference = found->second; }
        }, PREFAB_TYPE);
    }

    // The values and the moves, then the removals: what was moved out of a removed entity stays.
    for (Chosen &item : chosen) {
        Entity target = prefab.getById(item.source);
        if (item.removed || !target.isValid()) { continue; }
        if (item.patch.size() > 0) {
            Json objects;
            const Json now = objectOf(target);
            Json wanted = Json(now);
            merge(wanted, item.patch);
            applyObject(target, wanted, now);
            notePlace(places, *tmpl, item.source, item.patch);
        }
        if (item.parent != UUID(0)) {
            // Under an entity of the instance, or under an addition that comes with this Apply; one
            // moved out of the instance stays its own.
            const auto part = sources.find(item.parent);
            const auto copy = adopted.find(item.parent);
            Entity above = part != sources.end() ? prefab.getById(part->second) : copy != adopted.end() ? prefab.getById(copy->second) : Entity();
            if (above.isValid()) {
                prefab.setParent(target, above, /*keepWorldTransform*/ false);
                Json mask = Json::object();
                mask.add(PARENT, Json(true));
                notePlace(places, *tmpl, item.source, mask);
            }
        }
    }
    for (Chosen &item : chosen) {
        Entity target = prefab.getById(item.source);
        if (!item.removed || !target.isValid()) { continue; }
        // The other instances say nothing of it, nor of what goes with it, any more.
        for (Entity gone : prefab.subtree(target)) {
            Json everything = Json::object();
            everything.add(EVERYTHING, Json(true));
            notePlace(places, *tmpl, gone.getId(), everything);
        }
        prefab.destroy(target);
    }
    // What the entities of the prefab name of the additions (a field of the root that names an
    // added entity), they name by the copies.
    if (!adopted.empty()) {
        for (auto handle : prefab.getAllEntitiesWith<IdComponent>()) {
            types.forEachReference(prefab.entity(handle), [&adopted](UUID &reference) {
                const auto found = adopted.find(reference);
                if (found != adopted.end()) { reference = found->second; }
            }, PREFAB_TYPE);
        }
    }

    // In the world the additions are parts of the instance from now on: each is the entity of
    // the prefab its copy is, with the id of a part. An instance among them is nested now — its
    // root keeps its prefab.
    std::vector<std::pair<Entity, UUID>> parted;
    for (auto &[original, copy] : adopted) {
        Entity entity = world.getById(original);
        if (!entity.isValid()) { continue; }
        const bool nestedRoot = isRootOfInstance(entity);
        const AssetId nested = nestedRoot ? entity.getComponent<PrefabComponent>().prefab : AssetId(0);
        PrefabComponent &link = entity.hasComponent<PrefabComponent>() ? entity.getComponent<PrefabComponent>() : entity.addComponent<PrefabComponent>();
        link.instance = instance;
        link.source = copy;
        link.prefab = nested;
        parted.emplace_back(entity, copy);
    }
    std::sort(parted.begin(), parted.end(), [](const auto &l, const auto &r) { return l.second < r.second; });
    for (auto &[entity, source] : parted) {
        const UUID id = freeDerivedEntityId(instance, source, [&world](UUID id) { return static_cast<entt::entity>(world.getById(id)) != entt::null; });
        world.changeEntityId(entity, id);
    }
    applied.prefab = tmpl->id;
    applied.instance = instance;
    applied.reidentified = !parted.empty();
    applied.file = save(prefab, prefabRoot);
    applied.places = Rain::writeJson(places);
    return applied;
}

bool stripApplied(const Applied &applied, const std::string &file, std::string &result) {
    Json document;
    Json places;
    if (!Rain::parseJson(file, document) || !Rain::parseJson(applied.places, places) || !places.isArray()) { return false; }
    Json *entities = document.find(ENTITIES);
    if (entities == nullptr || !entities->isArray()) { return false; }
    bool changed = false;
    for (Json &entry : entities->items()) {
        if (entry.find(PREFAB) != nullptr) { changed = strip(entry, places, applied.prefab) || changed; }
    }
    if (changed) { result = Rain::writeJson(document, Rain::JsonLayout::Pretty); }
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
    std::vector<Entity> under = world.subtree(root);
    under.erase(under.begin());
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

} // namespace Panda::Prefabs

#endif // PND_EDITOR
