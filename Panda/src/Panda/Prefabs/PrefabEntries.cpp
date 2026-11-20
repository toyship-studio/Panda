// Prefabs (Prefabs.hpp): an entry ↔ the entities of an instance, and the files of the worlds that
// hold entries.

#include "Panda/Prefabs/Prefabs.hpp"

#ifdef PND_EDITOR

#    include "Panda/Prefabs/PrefabsInternal.hpp"

namespace Panda::Prefabs::Detail {

/* ------------------------------ Entities as JSON ------------------------------ */

Json objectOf(Entity entity) {
    return WorldMapper::saveEntity(entity);
}

void applyObject(Entity entity, const Json &target, const Json &current) {
    const ComponentTypeRegistry &types = GameContext::getExtensions().components;
    std::vector<ForeignValue> foreign;
    for (auto member = target.members().begin(); member != target.members().end(); ++member) {
        const char *key = member->key.c_str();
        if (std::strcmp(key, ID) == 0 || std::strcmp(key, RELATIONSHIP) == 0) { continue; }
        const ComponentType *type = types.find(key);
        if (type == nullptr) {
            foreign.push_back({key, Rain::writeJson(member->value)});
            continue;
        }
        const Json *now = current.find(key);
        if (now != nullptr && *now == member->value) { continue; }
        // One component the way a file holds it: `{"<type>": <value>}`.
        Json component = Json::object();
        component.add(key, Json(member->value));
        WorldMapper::loadComponent(entity, *type, component);
    }
    for (const ComponentType &type : types.types()) {
        if (!type.serialized || type.builtin || type.remove == nullptr) { continue; }
        if (entity.hasComponent(type.typeId) && target.find(type.name.c_str()) == nullptr) { type.remove(entity); }
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

/* ------------------------------ The entities of instances in a world ------------------------------ */

std::unordered_map<UUID, Parts> partsByInstance(World &world, UUID only) {
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

void resolveReferences(World &world, UUID instance) {
    const ComponentTypeRegistry &types = GameContext::getExtensions().components;
    for (auto &[root, parts] : partsByInstance(world, instance)) {
        std::unordered_map<UUID, UUID> ids; // the id in the prefab → the id in the world
        for (auto &[source, entity] : parts.live) { ids[source] = entity.getId(); }
        for (auto &[source, entity] : parts.live) {
            types.forEachReference(entity, [&](UUID &reference) {
                const auto found = ids.find(reference);
                if (found != ids.end()) { reference = found->second; }
            }, PREFAB_TYPE);
        }
    }
}

/* ------------------------------ An entry ↔ the entities of an instance ------------------------------ */

void expandEntry(
    const Template &tmpl, const Json &entry, const std::function<UUID(UUID)> &idFor, std::vector<ExpandedPart> &parts
) {
    const UUID instance = idIn(entry, ID);
    // What the entry says of the entities under the root, by their ids in the prefab.
    std::unordered_map<UUID, const Json *> overrides;
    if (const Json *list = entry.find(OVERRIDES); list != nullptr && list->isArray()) {
        for (const Json &item : list->items()) {
            const Json *named = item.find(ENTITY);
            const UUID source = named != nullptr ? resolvePath(tmpl, pathFrom(*named)) : UUID(0);
            if (source == UUID(0) || source == tmpl.root) {
                LOG_WARN_EDITOR(
                    "The instance %u of %s overrides an entity the prefab does not have (%s): the override is left out.",
                    static_cast<uint32_t>(instance), tmpl.nameOf(tmpl.root).c_str(), named != nullptr ? Rain::writeJson(*named).c_str() : "?"
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
            const Json *removed = found->second->find(REMOVED);
            if (removed != nullptr && removed->isBool() && removed->getBool()) { return false; }
            if (found->second->find(PARENT) != nullptr) { return true; }
        }
        return isThere(tmpl.parents.at(source));
    };
    for (UUID source : tmpl.ids) {
        if (!isThere(source)) { continue; }
        ExpandedPart part;
        part.source = source;
        part.id = source == tmpl.root ? instance : idFor(source);
        part.object = Json(*tmpl.objects.at(source));
        const auto found = overrides.find(source);
        if (source == tmpl.root) {
            Json patch = Json::object();
            for (auto member = entry.members().begin(); member != entry.members().end(); ++member) {
                const char *key = member->key.c_str();
                if (isReserved(key) || std::strcmp(key, PREFAB) == 0 || std::strcmp(key, OVERRIDES) == 0) { continue; }
                patch.add(key, Json(member->value));
            }
            merge(part.object, patch);
            part.parent = parentOf(entry);
        } else {
            const Json *placed = nullptr;
            if (found != overrides.end()) {
                Json patch = Json::object();
                for (auto member = found->second->members().begin(); member != found->second->members().end(); ++member) {
                    const char *key = member->key.c_str();
                    if (isReserved(key) || std::strcmp(key, ENTITY) == 0 || std::strcmp(key, REMOVED) == 0 || std::strcmp(key, PARENT) == 0) {
                        continue;
                    }
                    patch.add(key, Json(member->value));
                }
                merge(part.object, patch);
                placed = found->second->find(PARENT);
            }
            const UUID above = tmpl.parents.at(source);
            part.parent = placed != nullptr ? idOf(*placed) : above == tmpl.root ? instance : idFor(above);
        }
        // The object is of this instance now: its id, and where it came from. The root of an
        // instance nested in the prefab keeps the prefab it is of.
        const auto nested = tmpl.links.find(source);
        Json link = Json::object();
        link.add("instance", Json(static_cast<uint32_t>(instance)));
        link.add("source", Json(static_cast<uint32_t>(source)));
        link.add("prefab", Json(static_cast<uint32_t>(source == tmpl.root ? tmpl.id : nested != tmpl.links.end() ? nested->second.prefab : AssetId(0))));
        part.object.set(ID, Json(static_cast<uint32_t>(part.id)));
        part.object.erase(RELATIONSHIP);
        part.object.set(PREFAB_LINK_KEY, std::move(link));
        parts.push_back(std::move(part));
    }
}

Json buildEntry(
    const Template &tmpl, const Json &root, const std::unordered_map<UUID, const Json *> &parts,
    const std::function<bool(UUID)> &isImplied
) {
    Json entry = Json::object();
    entry.add(ID, Json(static_cast<uint32_t>(idIn(root, ID))));
    entry.add(PREFAB, Json(static_cast<uint32_t>(tmpl.id)));
    // Its place in the world, and the entities of the file under its root.
    Json relationship = Json::object();
    relationship.add(PARENT, Json(static_cast<uint32_t>(parentOf(root))));
    Json children = Json::array();
    const Json *rootRelationship = root.find(RELATIONSHIP);
    if (const Json *listed = rootRelationship != nullptr ? rootRelationship->find(CHILDREN) : nullptr; listed != nullptr && listed->isArray()) {
        for (const Json &child : listed->items()) {
            if (!isImplied(idOf(child))) { children.push(static_cast<uint32_t>(idOf(child))); }
        }
    }
    relationship.add(CHILDREN, std::move(children));
    entry.add(RELATIONSHIP, std::move(relationship));
    // What the root differs in. What it is called and where it stands are its own: the
    // name, the position and the rotation are always written, whatever the prefab has —
    // first, and in one order, so that an entry does not change its lines when a value
    // starts or stops being the one of the prefab.
    Json patch = Json::object();
    diffEntity(*tmpl.objects.at(tmpl.root), root, patch);
    if (const Json *tag = root.find(TAG)) { entry.add(TAG, Json(*tag)); }
    if (const Json *transform = root.find(TRANSFORM)) {
        Json own = Json::object();
        for (const char *key : {POSITION, ROTATION}) {
            if (const Json *value = transform->find(key)) { own.add(key, Json(*value)); }
        }
        if (Json *differing = patch.find(TRANSFORM); differing != nullptr && differing->isObject()) {
            for (auto member = differing->members().begin(); member != differing->members().end(); ++member) {
                const char *key = member->key.c_str();
                if (std::strcmp(key, POSITION) != 0 && std::strcmp(key, ROTATION) != 0) { own.add(member->key, std::move(member->value)); }
            }
        }
        entry.add(TRANSFORM, std::move(own));
    }
    for (auto member = patch.members().begin(); member != patch.members().end(); ++member) {
        const char *key = member->key.c_str();
        if (std::strcmp(key, TAG) != 0 && std::strcmp(key, TRANSFORM) != 0) { entry.add(member->key, std::move(member->value)); }
    }
    // What the entities under it differ in.
    Json overrides = Json::array();
    for (UUID source : tmpl.ids) {
        if (source == tmpl.root) { continue; }
        const auto part = parts.find(source);
        const auto above = parts.find(tmpl.parents.at(source));
        std::vector<UUID> path;
        if (!pathOf(tmpl, source, path)) { continue; }
        if (part == parts.end()) {
            // Removed — said for the topmost one: what was under it went with it.
            if (above != parts.end()) {
                Json removed = Json::object();
                removed.add(ENTITY, pathJson(path));
                removed.add(REMOVED, Json(true));
                overrides.push(std::move(removed));
            }
            continue;
        }
        Json item = Json::object();
        item.add(ENTITY, pathJson(path));
        diffEntity(*tmpl.objects.at(source), *part->second, item);
        // It stands under another entity than the prefab says — or what it stood under is gone.
        const UUID parent = parentOf(*part->second);
        if (above == parts.end() || parent != idIn(*above->second, ID)) {
            item.add(PARENT, Json(static_cast<uint32_t>(parent)));
        }
        if (item.size() > 1) { overrides.push(std::move(item)); }
    }
    if (!overrides.empty()) { entry.add(OVERRIDES, std::move(overrides)); }
    return entry;
}

bool readEntry(Entity root, Json &entry, const Template *&tmpl) {
    if (!isRootOfInstance(root)) { return false; }
    tmpl = templateOf(root.getComponent<PrefabComponent>().prefab);
    if (tmpl == nullptr) { return false; }
    World &world = *root.getWorld();
    Parts parts = partsByInstance(world, root.getId())[root.getId()];
    Json objects; // owns the objects of the entities
    std::unordered_map<UUID, Json> read;
    {
        TemplateSpace space(world, root.getId());
        for (auto &[source, entity] : parts.live) { read[source] = objectOf(entity); }
    }
    std::unordered_map<UUID, const Json *> byId;
    for (auto &[source, object] : read) { byId[source] = &object; }
    const auto rootObject = byId.find(tmpl->root);
    if (rootObject == byId.end()) { return false; }
    const auto isImplied = [&world](UUID id) {
        Entity child = world.getById(id);
        return child.isValid() && child.hasComponent<PrefabComponent>() && child.getComponent<PrefabComponent>().instance != id;
    };
    entry = buildEntry(*tmpl, *rootObject->second, byId, isImplied);
    return true;
}

} // namespace Panda::Prefabs::Detail

namespace Panda::Prefabs {

using namespace Detail;

/* ------------------------------ The files ------------------------------ */

namespace {

    // The entries of the instances a document holds become the entities they stand for, in place.
    // false — it holds none: nothing changed.
    bool expandEntries(Json &document) {
        Json *entities = document.find(ENTITIES);
        bool any = false;
        if (entities != nullptr && entities->isArray()) {
            for (const Json &entry : entities->items()) { any = any || entry.find(PREFAB) != nullptr; }
        }
        if (!any) { return false; }
        // The ids of the file are taken; the ids of the entities of an instance are derived around
        // them, the instances in the order of their ids.
        std::unordered_set<UUID> taken;
        std::vector<const Json *> instances;
        for (const Json &entry : entities->items()) {
            taken.insert(idIn(entry, ID));
            if (entry.find(PREFAB) != nullptr) { instances.push_back(&entry); }
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
                Json entity = Json::object();
                entity.add(ID, Json(static_cast<uint32_t>(instance)));
                const Json *relationship = entry->find(RELATIONSHIP);
                entity.add(RELATIONSHIP, relationship != nullptr ? Json(*relationship) : Json::object());
                Json tag = Json::object();
                tag.add("tag", Json("Missing Prefab"));
                entity.add(TAG, std::move(tag));
                Json link = Json::object();
                link.add("instance", Json(static_cast<uint32_t>(instance)));
                link.add("source", Json(0u));
                link.add("prefab", Json(static_cast<uint32_t>(prefab)));
                entity.add(PREFAB_LINK_KEY, std::move(link));
                entity.add(MISSING, Json(*entry));
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
            expandEntry(*tmpl, *entry, [&ids](UUID source) { return ids.at(source); }, expandedOf[instance]);
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
        for (const Json &entry : entities->items()) {
            const UUID parent = parentOf(entry);
            if (parent != UUID(0)) { listed[parent].push_back(idIn(entry, ID)); }
        }
        Json result = Json::array();
        for (Json &entry : entities->items()) {
            const UUID id = idIn(entry, ID);
            if (const auto alone = lone.find(id); alone != lone.end()) {
                result.push(std::move(alone->second));
                continue;
            }
            const auto parts = expandedOf.find(id);
            if (parts == expandedOf.end()) {
                // An entity of the file: the entities of instances moved under it join its list.
                if (const auto more = under.find(id); more != under.end()) {
                    Json *relationship = entry.find(RELATIONSHIP);
                    if (relationship != nullptr && relationship->isObject()) {
                        if (relationship->find(CHILDREN) == nullptr) { relationship->add(CHILDREN, Json::array()); }
                        Json &children = *relationship->find(CHILDREN);
                        for (UUID child : more->second) { children.push(static_cast<uint32_t>(child)); }
                    }
                }
                result.push(std::move(entry));
                continue;
            }
            for (ExpandedPart &part : parts->second) {
                Json relationship = Json::object();
                relationship.add(PARENT, Json(static_cast<uint32_t>(part.parent)));
                Json children = Json::array();
                std::unordered_set<UUID> seen;
                const auto push = [&](UUID child) {
                    if (seen.insert(child).second) { children.push(static_cast<uint32_t>(child)); }
                };
                if (const auto more = under.find(part.id); more != under.end()) {
                    for (UUID child : more->second) { push(child); }
                }
                if (const auto more = listed.find(part.id); more != listed.end()) {
                    for (UUID child : more->second) {
                        if (!partIds.contains(child)) { push(child); }
                    }
                }
                relationship.add(CHILDREN, std::move(children));
                part.object.add(RELATIONSHIP, std::move(relationship));
                result.push(std::move(part.object));
            }
        }
        *entities = std::move(result);
        return true;
    }

    // The other way: the entities of instances become the entries of their instances, in place.
    // false — the document has no entities.
    bool collapseEntries(Json &document) {
        Json *entities = document.find(ENTITIES);
        if (entities == nullptr || !entities->isArray()) { return false; }
        // The entities of instances, by the root of their instance and their id in the prefab.
        std::unordered_map<UUID, std::unordered_map<UUID, const Json *>> partsOf;
        std::unordered_set<UUID> implied; // the entities a file does not list: parts that are not roots
        std::unordered_set<UUID> roots;
        for (const Json &entity : entities->items()) {
            const Json *link = entity.find(PREFAB_LINK_KEY);
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
        Json result = Json::array();
        std::unordered_set<UUID> written; // the entities that stay entities of the file
        for (Json &entity : entities->items()) {
            const UUID id = idIn(entity, ID);
            const Json *link = entity.find(PREFAB_LINK_KEY);
            if (link == nullptr) {
                result.push(std::move(entity));
                continue;
            }
            const UUID instance = idIn(*link, "instance");
            if (instance != id) {
                if (implied.contains(id)) { continue; } // said by the entry of its instance
                entity.erase(PREFAB_LINK_KEY);         // its instance is gone: an entity like any other
                result.push(std::move(entity));
                continue;
            }
            const Template *tmpl = templateOf(idIn(*link, "prefab"));
            if (tmpl == nullptr) {
                // The prefab is not there. The entry that was read is written back, at the place the
                // entity stands now; without one the entity stays an entity.
                if (Json *kept = entity.find(MISSING)) {
                    Json entry = Json(*kept);
                    if (const Json *relationship = entity.find(RELATIONSHIP)) { entry.set(RELATIONSHIP, Json(*relationship)); }
                    result.push(std::move(entry));
                } else {
                    entity.erase(PREFAB_LINK_KEY);
                    for (auto &[source, part] : partsOf[instance]) { implied.erase(idIn(*part, ID)); }
                    result.push(std::move(entity));
                }
                continue;
            }
            result.push(buildEntry(*tmpl, entity, partsOf[instance], isImplied));
        }
        // A part whose prefab turned out to be missing is an entity of the file after all.
        for (Json &entity : entities->items()) {
            if (!entity.isObject()) { continue; } // moved into the result
            const Json *link = entity.find(PREFAB_LINK_KEY);
            if (link != nullptr && !implied.contains(idIn(entity, ID)) && idIn(*link, "instance") != idIn(entity, ID)) {
                entity.erase(PREFAB_LINK_KEY);
                result.push(std::move(entity));
            }
        }
        // An entity of the file lists the entities of the file under it and no others.
        for (Json &entity : result.items()) {
            Json *relationship = entity.find(RELATIONSHIP);
            Json *children = relationship != nullptr ? relationship->find(CHILDREN) : nullptr;
            if (children == nullptr || !children->isArray()) { continue; }
            for (auto child = children->items().begin(); child != children->items().end();) {
                child = implied.contains(idOf(*child)) ? children->items().erase(child) : child + 1;
            }
        }
        *entities = std::move(result);
        return true;
    }

} // namespace

bool expand(const std::string &file, std::string &expanded) {
    Json document;
    if (!Rain::parseJson(file, document)) { return false; }
    expanded = expandEntries(document) ? Rain::writeJson(document) : file;
    return true;
}

std::string collapse(const std::string &expanded) {
    Json document;
    if (!Rain::parseJson(expanded, document) || !collapseEntries(document)) { return expanded; }
    return Rain::writeJson(document, Rain::JsonLayout::Pretty);
}

bool load(World &world, const std::string &text) {
    Json document;
    if (!Rain::parseJson(text, document)) { return false; }
    expandEntries(document);
    if (!WorldMapper::load(world, document)) { return false; }
    resolveReferences(world);
    world.setChanged(false);
    return true;
}

std::string save(World &world, Entity root) {
    Json document;
    const bool instances = world.getAllEntitiesWith<PrefabComponent>().begin() != world.getAllEntitiesWith<PrefabComponent>().end();
    {
        // The entities of instances are written as a file names them, and put back as they were.
        std::unique_ptr<TemplateSpace> space = instances ? std::make_unique<TemplateSpace>(world) : nullptr;
        document = root.isValid() ? WorldMapper::saveTree(world, root) : WorldMapper::save(world);
    }
    if (instances) { collapseEntries(document); }
    // The root of a prefab file stands under nothing.
    if (root.isValid() && root.getParent().isValid()) {
        if (Json *entities = document.find(ENTITIES); entities != nullptr && entities->isArray()) {
            for (Json &entity : entities->items()) {
                Json *relationship = entity.find(RELATIONSHIP);
                if (idIn(entity, ID) == root.getId() && relationship != nullptr) { relationship->set(PARENT, Json(0u)); }
            }
        }
    }
    return Rain::writeJson(document, Rain::JsonLayout::Pretty);
}

} // namespace Panda::Prefabs

#endif // PND_EDITOR
