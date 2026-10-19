#include "Panda/Serialization/WorldMapper.hpp"
#include "Panda/Extensions/Extensions.hpp"
#include "Panda/GameLogic/GameContext.hpp"
#include "Panda/Serialization/ComponentDtos.hpp"
#include "Panda/Serialization/SettingsMapper.hpp"

#include <set>

namespace Panda {

namespace {

    // --- World document: encoded as a single value (Rain calls the static encode/decode).

    struct WorldDocument {
        World *world = nullptr;

        static void encode(const char *key, Rain::Encoder *encoder, const WorldDocument &document) {
            World &world = *document.world;
            const ComponentTypeRegistry &types = GameContext::getExtensions().components;
            encoder->beginObject(key);
            SettingsMapper::encode(SettingsScope::World, world.context(), *encoder);
            size_t droppedForeign = 0; // foreign components a coder without opaque values leaves out
            encoder->beginArray("entities");
            for (auto entityHandle : world.getAllEntitiesWith<IdComponent>()) {
                if (!world.isValidEntt(entityHandle)) { continue; }
                Entity entity = world.entity(entityHandle);
                encoder->beginObject(nullptr);
                encoder->encode("id", entity.getId());
                for (const ComponentType &type : types.types()) {
                    if (!type.serialized || !entity.hasComponent(type.typeId)) { continue; }
                    type.encode(*encoder, entity);
                }
                if (entity.hasComponent<ForeignComponents>()) {
                    const ForeignComponents &foreign = entity.getComponent<ForeignComponents>();
                    if (!encoder->supportsRawJson()) {
                        droppedForeign += foreign.items.size();
                    } else {
                        for (const ForeignValue &item : foreign.items) {
                            encoder->encodeRawJson(item.name.c_str(), item.json);
                        }
                    }
                }
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
            World &world = *document.world;
            const ComponentTypeRegistry &types = GameContext::getExtensions().components;
            world.clear();
            SettingsMapper::decode(SettingsScope::World, world.context(), *decoder, {"entities"});
            std::set<std::string> foreignKept, foreignDropped; // one warning per component key
            if (decoder->beginArray("entities")) {
                while (decoder->arrayHasElement()) {
                    if (!decoder->beginObject(nullptr)) { break; }
                    UUID id = 0;
                    decoder->decode("id", id);
                    Entity entity = world.instantiateEntity(id);
                    for (const std::string &name : decoder->getMemberKeys()) {
                        if (name == "id") { continue; }
                        if (const ComponentType *type = types.find(name)) {
                            if (type->decode != nullptr) { type->decode(*decoder, entity); }
                            continue;
                        }
                        // A key without a contribution — the module is not loaded: keep it as is.
                        std::string json;
                        if (!decoder->decodeRawJson(name.c_str(), json)) {
                            foreignDropped.insert(name);
                            continue;
                        }
                        foreignKept.insert(name);
                        ForeignComponents &foreign = entity.hasComponent<ForeignComponents>()
                                                         ? entity.getComponent<ForeignComponents>()
                                                         : entity.addComponent<ForeignComponents>();
                        foreign.items.push_back({name, std::move(json)});
                    }
                    decoder->endObject();
                    decoder->arrayNext();
                }
                decoder->endArray();
            }
            decoder->endObject();
            for (const std::string &name : foreignKept) {
                LOG_WARN("Component \"%s\": no module provides it, kept as it is", name.c_str());
            }
            for (const std::string &name : foreignDropped) {
                LOG_WARN("Component \"%s\": no module provides it, dropped", name.c_str());
            }
            world.setChanged(false);
            world.sort();
            return true;
        }
    };

} // namespace

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

} // namespace Panda
