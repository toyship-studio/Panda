#include "Panda/Serialization/WorldMapper.hpp"
#include "Panda/Extensions/Extensions.hpp"
#include "Panda/GameLogic/GameContext.hpp"
#include "Panda/Prefabs/PrefabComponent.hpp"
#include "Panda/Serialization/ComponentDtos.hpp"

#include <Rain/Coders/JsonDecoder.hpp>
#include <Rain/Coders/JsonEncoder.hpp>

#include <algorithm>

#include <set>

namespace Panda {

namespace {

    // An entity as the `entities` of a world file hold it: the id, then the components by the
    // names of their types. Returns the number of foreign components a coder without raw values
    // left out.
    size_t encodeEntity(Rain::Encoder &encoder, Entity entity, const ComponentTypeRegistry &types) {
        size_t droppedForeign = 0;
        encoder.encode("id", entity.getId());
        for (const ComponentType &type : types.types()) {
            if (!type.serialized || !entity.hasComponent(type.typeId)) { continue; }
            type.encode(encoder, entity);
        }
        if (entity.hasComponent<ForeignComponents>()) {
            const ForeignComponents &foreign = entity.getComponent<ForeignComponents>();
            if (!encoder.supportsRaw()) {
                droppedForeign += foreign.items.size();
            } else {
                for (const ForeignValue &item : foreign.items) { encoder.encodeRaw(item.name.c_str(), item.value); }
            }
        }
        return droppedForeign;
    }

    // What the reading of entities has to say, once per component key.
    struct EntityIssues {
        std::set<std::string> foreignKept, foreignDropped, unread;

        void report() const {
            for (const std::string &name : foreignKept) {
                LOG_WARN("Component \"%s\": no module provides it, kept as it is", name.c_str());
            }
            for (const std::string &name : foreignDropped) {
                LOG_WARN("Component \"%s\": no module provides it, dropped", name.c_str());
            }
            for (const std::string &name : unread) {
                LOG_WARN(
                    "Component \"%s\": its value is not read — a field of another type or shape than the format has; "
                    "the entity is left without the component",
                    name.c_str()
                );
            }
        }
    };

    // The components of an entity from the object the decoder stands in: every key but `id` is a
    // component by the name of its type; a key without a type — a module that is not loaded — is
    // kept as it is.
    void decodeEntity(Rain::Decoder &decoder, Entity entity, const ComponentTypeRegistry &types, EntityIssues &issues) {
        for (const std::string &name : decoder.getMemberKeys()) {
            if (name == "id") { continue; }
            if (const ComponentType *type = types.find(name)) {
                if (type->decode != nullptr) {
                    type->decode(decoder, entity);
                    // A component its decode adds, and the entity is without it: the
                    // value under the key is not what the format has there.
                    if (!type->builtin && !entity.hasComponent(type->typeId)) { issues.unread.insert(name); }
                }
                continue;
            }
            // A key without a contribution — the module is not loaded: keep it as is.
            Rain::Value value;
            if (!decoder.decodeRaw(name.c_str(), value)) {
                issues.foreignDropped.insert(name);
                continue;
            }
            if (name != PREFAB_MISSING_KEY) { issues.foreignKept.insert(name); } // kept on purpose: nothing to tell
            ForeignComponents &foreign = entity.hasComponent<ForeignComponents>()
                                             ? entity.getComponent<ForeignComponents>()
                                             : entity.addComponent<ForeignComponents>();
            foreign.items.push_back({name, std::move(value)});
        }
    }

    // --- World document: encoded as a single value (Rain calls the static encode/decode).

    // Of a kind a world uses one of (ComponentType::isMain: the camera, the sun, the sky, the
    // wind) a file names one main component. A file written by hand may name two — a component is
    // the main one unless it says otherwise: said here, because the world uses one of them and
    // nothing in the file tells which.
    void reportMains(World &world, const ComponentTypeRegistry &types) {
        for (const ComponentType &type : types.types()) {
            if (type.isMain == nullptr) { continue; }
            const std::vector<Entity> mains = world.findMains(type);
            if (mains.size() < 2) { continue; }
            // By name and id: names repeat, and the id is what the file is searched by.
            const auto named = [](const Entity &entity) {
                return entity.getName() + " (id " + std::to_string(static_cast<uint32_t>(entity.getId())) + ")";
            };
            std::string names;
            for (const Entity &main : mains) { names += (names.empty() ? "" : ", ") + named(main); }
            LOG_WARN_EDITOR(
                "World: %zu entities have a main %s — %s — and a world uses one: %s now. Write \"isMain\": false "
                "into the others.",
                mains.size(), type.name.c_str(), names.c_str(), named(mains.front()).c_str()
            );
        }
    }

    struct WorldDocument {
        World *world = nullptr;
        // Not 0 — the entities of one tree, from this root down, and no others (a prefab).
        UUID root = 0;

        static void encode(const char *key, Rain::Encoder *encoder, const WorldDocument &document) {
            World &world = *document.world;
            const ComponentTypeRegistry &types = GameContext::getExtensions().components;
            encoder->beginObject(key);
            size_t droppedForeign = 0; // foreign components a coder without raw values leaves out
            encoder->beginArray("entities");
            // The file lists the entities the way the Hierarchy shows them (World::orderedChildren):
            // an entity, then everything under it. The order depends on the world alone — not on
            // how its entities happen to lie in memory — so saving a world twice writes the same
            // file, and an edit is a small diff.
            const bool whole = document.root == UUID(0);
            std::vector<UUID> pending = whole ? world.orderedChildren(UUID(0)) : std::vector<UUID>{document.root};
            std::reverse(pending.begin(), pending.end()); // a stack: the first sibling on top
            std::set<UUID> written;
            // An entity the walk from the roots does not reach (a parent that lists it no more)
            // is not lost: it is written after the tree, by id.
            std::vector<UUID> unreached;
            for (auto entityHandle : world.getAllEntitiesWith<IdComponent>()) {
                if (whole && world.isValidEntt(entityHandle)) { unreached.push_back(world.entity(entityHandle).getId()); }
            }
            std::sort(unreached.begin(), unreached.end(), [](UUID l, UUID r) { return r < l; }); // popped from the back: ascending
            while (!pending.empty() || !unreached.empty()) {
                if (pending.empty()) {
                    const UUID id = unreached.back();
                    unreached.pop_back();
                    if (!written.contains(id)) { pending.push_back(id); }
                    continue;
                }
                Entity entity = world.getById(pending.back());
                pending.pop_back();
                if (!entity.isValid() || !written.insert(entity.getId()).second) { continue; }
                std::vector<UUID> children = world.orderedChildren(entity.getId());
                pending.insert(pending.end(), children.rbegin(), children.rend());
                encoder->beginObject(nullptr);
                droppedForeign += encodeEntity(*encoder, entity, types);
                encoder->endObject();
            }
            encoder->endArray();
            encoder->endObject();
            if (droppedForeign > 0) {
                LOG_WARN(
                    "%zu component(s) of modules that are not loaded are not written: the format carries only "
                    "registered data",
                    droppedForeign
                );
            }
        }

        static bool decode(const char *key, Rain::Decoder *decoder, WorldDocument &document) {
            if (!decoder->beginObject(key)) { return false; }
            // A world file is its entities. A key beside them is not read and a save leaves it
            // out: said, not lost in silence — the file is written by hand and by agents too.
            for (const std::string &name : decoder->getMemberKeys()) {
                if (name == "entities") { continue; }
                LOG_WARN_EDITOR(
                    "World: \"%s\" is not a part of a world file (a world is its entities): it is not read, and a save "
                    "leaves it out.",
                    name.c_str()
                );
            }
            World &world = *document.world;
            const ComponentTypeRegistry &types = GameContext::getExtensions().components;
            world.clear();
            EntityIssues issues;
            if (decoder->beginArray("entities")) {
                while (decoder->arrayHasElement()) {
                    if (!decoder->beginObject(nullptr)) { break; }
                    UUID id = 0;
                    decoder->decode("id", id);
                    // An id is one entity's: a file written by hand that gives one to two, or
                    // none at all, is read with a new id for the entity — what names the id
                    // means the first one — and says so.
                    const bool taken = id != UUID(0) && static_cast<entt::entity>(world.getById(id)) != entt::null;
                    Entity entity = id == UUID(0) || taken ? world.instantiateEntity() : world.instantiateEntity(id);
                    decodeEntity(*decoder, entity, types, issues);
                    if (taken) {
                        LOG_ERROR_EDITOR(
                            "World: two entities have the id %u; \"%s\" got the id %u instead — give it an id of its own in the file.",
                            static_cast<uint32_t>(id), entity.getName().c_str(), static_cast<uint32_t>(entity.getId())
                        );
                    } else if (id == UUID(0)) {
                        LOG_ERROR_EDITOR(
                            "World: the entity \"%s\" has no id; it got the id %u — write it into the file.", entity.getName().c_str(),
                            static_cast<uint32_t>(entity.getId())
                        );
                    }
                    decoder->endObject();
                    decoder->arrayNext();
                }
                decoder->endArray();
            }
            decoder->endObject();
            issues.report();
            reportMains(world, types);
            // A file written before the encoder dropped the tombstones of undoable deletions lists
            // children that do not exist; the sort and the hierarchy need every link to resolve.
            if (const size_t repaired = world.repairHierarchy()) {
                LOG_WARN_EDITOR("World: %zu broken hierarchy link(s) repaired; save the world to keep the repair", repaired);
            }
            world.setChanged(false);
            return true;
        }
    };

    // --- One entity: the object of its entry in the `entities` of a world file.

    struct EntityDocument {
        Entity entity;

        static void encode(const char *key, Rain::Encoder *encoder, const EntityDocument &document) {
            encoder->beginObject(key);
            encodeEntity(*encoder, document.entity, GameContext::getExtensions().components);
            encoder->endObject();
        }

        static bool decode(const char *key, Rain::Decoder *decoder, EntityDocument &document) {
            if (!decoder->beginObject(key)) { return false; }
            EntityIssues issues;
            decodeEntity(*decoder, document.entity, GameContext::getExtensions().components, issues);
            decoder->endObject();
            issues.report();
            return true;
        }
    };

    // --- One component: the object a world file holds for it under an entity.

    struct ComponentDocument {
        Entity entity;
        const ComponentType *type = nullptr;

        static void encode(const char *key, Rain::Encoder *encoder, const ComponentDocument &document) {
            encoder->beginObject(key);
            if (document.type->encode != nullptr) { document.type->encode(*encoder, document.entity); }
            encoder->endObject();
        }

        static bool decode(const char *key, Rain::Decoder *decoder, ComponentDocument &document) {
            if (!decoder->beginObject(key)) { return false; }
            bool found = false;
            for (const std::string &name : decoder->getMemberKeys()) {
                if (name == document.type->name) { found = true; }
            }
            if (found && document.type->decode != nullptr) {
                // decode adds an optional component: the one that is there makes room first.
                if (!document.type->builtin && document.type->remove != nullptr &&
                    document.entity.hasComponent(document.type->typeId)) {
                    document.type->remove(document.entity);
                }
                document.type->decode(*decoder, document.entity);
            }
            decoder->endObject();
            return found;
        }
    };

} // namespace

void WorldMapper::saveComponent(Entity entity, const ComponentType &type, Rain::Encoder &encoder, std::ostream &stream) {
    ComponentDocument document;
    document.entity = entity;
    document.type = &type;
    encoder.encode(stream, document);
}

bool WorldMapper::loadComponent(Entity entity, const ComponentType &type, Rain::Decoder &decoder, std::istream &stream) {
    ComponentDocument document;
    document.entity = entity;
    document.type = &type;
    return decoder.decode(stream, document);
}

void WorldMapper::saveEntity(Entity entity, Rain::Encoder &encoder, std::ostream &stream) {
    EntityDocument document;
    document.entity = entity;
    encoder.encode(stream, document);
}

bool WorldMapper::loadEntity(Entity entity, Rain::Decoder &decoder, std::istream &stream) {
    EntityDocument document;
    document.entity = entity;
    return decoder.decode(stream, document);
}

void WorldMapper::saveTree(World &world, Entity root, Rain::Encoder &encoder, std::ostream &stream) {
    WorldDocument document;
    document.world = &world;
    document.root = root.getId();
    encoder.encode(stream, document);
}

void WorldMapper::save(const World &world, Rain::Encoder &encoder, std::ostream &stream) {
    WorldDocument document;
    document.world = const_cast<World *>(&world); // the registry traversal is a non-const World API
    encoder.encode(stream, document);
}

bool WorldMapper::load(World &world, Rain::Decoder &decoder, std::istream &stream) {
    WorldDocument document;
    document.world = &world;
    return decoder.decode(stream, document);
}

Rain::Value WorldMapper::save(const World &world) {
    WorldDocument document;
    document.world = const_cast<World *>(&world);
    return Rain::JsonEncoder().encodeValue(document);
}

bool WorldMapper::load(World &world, const Rain::Value &value) {
    WorldDocument document;
    document.world = &world;
    return Rain::JsonDecoder().decodeValue(value, document);
}

Rain::Value WorldMapper::saveTree(World &world, Entity root) {
    WorldDocument document;
    document.world = &world;
    document.root = root.getId();
    return Rain::JsonEncoder().encodeValue(document);
}

Rain::Value WorldMapper::saveEntity(Entity entity) {
    EntityDocument document;
    document.entity = entity;
    return Rain::JsonEncoder().encodeValue(document);
}

Rain::Value WorldMapper::saveComponent(Entity entity, const ComponentType &type) {
    ComponentDocument document;
    document.entity = entity;
    document.type = &type;
    return Rain::JsonEncoder().encodeValue(document);
}

bool WorldMapper::loadComponent(Entity entity, const ComponentType &type, const Rain::Value &value) {
    ComponentDocument document;
    document.entity = entity;
    document.type = &type;
    return Rain::JsonDecoder().decodeValue(value, document);
}

// A fragment goes through a scratch world on both sides: the entities are copied into it and it is
// written, or it is read like any world (foreign components kept, the hierarchy repaired — a
// parent outside the fragment makes a root) and its roots are copied out. One reader and one
// writer serve the file and the fragment.
void WorldMapper::saveEntities(
    World &world, const std::vector<Entity> &entities, Rain::Encoder &encoder, std::ostream &stream
) {
    World fragment;
    for (Entity entity : entities) {
        // An entity under another one of the set travels with its ancestor.
        bool covered = false;
        for (Entity above = entity.getParent(); above.isValid() && !covered; above = above.getParent()) {
            for (Entity other : entities) {
                if (other == above) { covered = true; }
            }
        }
        if (covered) { continue; }
        Entity copy = fragment.importEntity(world, entity);
        // A root of the fragment stands where the entity stood in its world, whatever was above it.
        if (entity.getParent().isValid()) { copy.getTransform().setTransform(world.getWorldSpaceTransformMatrix(entity)); }
    }
    WorldDocument document;
    document.world = &fragment;
    encoder.encode(stream, document);
}

bool WorldMapper::loadEntities(
    World &world, Rain::Decoder &decoder, std::istream &stream, std::vector<Entity> &added
) {
    World fragment;
    WorldDocument document;
    document.world = &fragment;
    if (!decoder.decode(stream, document)) { return false; }
    for (auto entityHandle : fragment.getAllEntitiesWith<IdComponent>()) {
        if (!fragment.isValidEntt(entityHandle)) { continue; }
        Entity entity = fragment.entity(entityHandle);
        if (!entity.getParent().isValid()) { added.push_back(world.importEntity(fragment, entity)); }
    }
    return true;
}

} // namespace Panda
