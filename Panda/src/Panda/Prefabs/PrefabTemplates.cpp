// Prefabs (Prefabs.hpp): the templates — a prefab as its instances are compared with it — and what
// follows a prefab that changed.

#include "Panda/Prefabs/Prefabs.hpp"

#ifdef PND_EDITOR

#    include "Panda/Prefabs/PrefabsInternal.hpp"

namespace Panda::Prefabs::Detail {

namespace {

    std::unordered_map<AssetId, std::unique_ptr<Template>> s_templates;

    // The prefabs being read right now: one that asks for itself on the way holds itself.
    std::vector<AssetId> s_loading;

    // prefabChanged: the prefabs to be read again when next asked for, and who reads them.
    std::unordered_set<AssetId> s_stale;

    const std::function<void(AssetId)> *s_reload = nullptr;

} // namespace

/* ------------------------------ Templates ------------------------------ */

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
    made->document = WorldMapper::saveTree(asset->world(), root);
    const Json *entities = made->document.find(ENTITIES);
    if (entities == nullptr || !entities->isArray()) { return nullptr; }
    for (const Json &object : entities->items()) {
        const UUID id = idIn(object, ID);
        made->ids.push_back(id);
        made->objects[id] = &object;
        made->parents[id] = id == made->root ? UUID(0) : parentOf(object);
        if (const Json *link = object.find(PREFAB_LINK_KEY)) {
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

UUID resolvePath(const Template &tmpl, const std::vector<UUID> &path, size_t from) {
    if (from >= path.size()) { return UUID(0); }
    if (from + 1 == path.size()) { return tmpl.objects.contains(path[from]) ? path[from] : UUID(0); }
    const auto root = tmpl.links.find(path[from]);
    const Template *nested = root != tmpl.links.end() ? templateOf(root->second.prefab) : nullptr;
    if (nested == nullptr) { return UUID(0); }
    const UUID inner = resolvePath(*nested, path, from + 1);
    const auto part = tmpl.parts.find({static_cast<uint32_t>(path[from]), static_cast<uint32_t>(inner)});
    return inner != UUID(0) && part != tmpl.parts.end() ? part->second : UUID(0);
}

Json pathJson(const std::vector<UUID> &path) {
    if (path.size() == 1) { return Json(static_cast<uint32_t>(path.front())); }
    Json array = Json::array();
    for (UUID id : path) { array.push(static_cast<uint32_t>(id)); }
    return array;
}

std::vector<UUID> pathFrom(const Json &value) {
    std::vector<UUID> path;
    if (value.isUint()) {
        path.push_back(UUID(value.getUint()));
    } else if (value.isArray()) {
        for (const Json &item : value.items()) { path.push_back(idOf(item)); }
    }
    return path;
}

} // namespace Panda::Prefabs::Detail

namespace Panda::Prefabs {

using namespace Detail;

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
    Json places;
    if (forced != nullptr) { Rain::parseJson(forced->places, places); }
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
            Json entry;
            const Template *tmpl = nullptr;
            if (!readEntry(root, entry, tmpl)) { continue; }
            // Apply Forced: the other instances lose what they had at the applied places — the
            // Revert of those places. The one the difference was taken from has nothing to save
            // for it either way: its file holds what the prefab now has.
            if (forced != nullptr && places.isArray()) {
                if (root.getId() == forced->instance || strip(entry, places, forced->prefab)) { dirty[world] = true; }
            }
            kept.push_back({world, root.getId(), Rain::writeJson(entry)});
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
