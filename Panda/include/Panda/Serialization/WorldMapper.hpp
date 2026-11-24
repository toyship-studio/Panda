#pragma once

#include "Panda/Extensions/ComponentTypes.hpp"
#include "Panda/GameLogic/World.hpp"

#include <Rain/Rain.hpp>
#include <Rain/Value.hpp>

#include <istream>
#include <ostream>

namespace Panda {

// World ↔ stream (the editor's .world is JSON, the export is binary; any coder). A world file is
// its entities and nothing else — what a world looks like is components of its entities (the sky,
// the sun, the wind), what a frame of it may cost is the project's settings. The entities go by
// the component type registry (Extensions::components): key = type name, the value is written and
// read by the contribution; a key without a contribution (the module is not loaded) is kept as is
// (ForeignComponents) and written back — by the JSON coder; the binary one carries only
// registered data. The format — PandaSDK/docs/world-format.md.
class WorldMapper {
public:
    static void save(const World &world, Rain::Encoder &encoder, std::ostream &stream);
    // Clears the world and fills it from the stream. false — the stream was not parsed; on a
    // parse error the world is untouched, a content error leaves a partially filled world.
    static bool load(World &world, Rain::Decoder &decoder, std::istream &stream);
    // A fragment of a world: the given entities with everything under them, in the format of the
    // world file — `{"entities": [...]}`. The text form of a selection: the clipboard of the
    // editor, a snippet an agent writes.
    static void saveEntities(
        World &world, const std::vector<Entity> &entities, Rain::Encoder &encoder, std::ostream &stream
    );
    // Adds the entities of a fragment (or of a whole world file) to the world as new roots with
    // new ids; `added` gets the roots. false — the stream was not parsed, the world is untouched.
    static bool loadEntities(
        World &world, Rain::Decoder &decoder, std::istream &stream, std::vector<Entity> &added
    );
    // One tree of the world — `root` and everything under it — as it is, with its ids, in the
    // format of a fragment: `{"entities": [...]}`. What the file of a prefab holds.
    static void saveTree(World &world, Entity root, Rain::Encoder &encoder, std::ostream &stream);
    // One entity in the form the `entities` of a world file hold it: an object with its id and
    // its components by the names of their types.
    static void saveEntity(Entity entity, Rain::Encoder &encoder, std::ostream &stream);
    // The components of such an object into an entity that was just made
    // (World::instantiateEntity): the id and the place in the hierarchy are not read. false —
    // the stream was not parsed.
    static bool loadEntity(Entity entity, Rain::Decoder &decoder, std::istream &stream);
    // One component of an entity in the form the world file holds it — an object with the name
    // of the type as its key: `{"pointLightComponent": {...}}`. The text form of a component:
    // the clipboard of the inspector, a snippet an agent writes.
    static void saveComponent(Entity entity, const ComponentType &type, Rain::Encoder &encoder, std::ostream &stream);
    // The value of the component from such a text into the entity: the component it has is
    // replaced, one it does not have is added. false — the stream was not parsed or does not
    // hold this component; the entity is untouched then.
    static bool loadComponent(Entity entity, const ComponentType &type, Rain::Decoder &decoder, std::istream &stream);

    // The same as JSON values (Rain::Value): the form of the file, for the code that works on it
    // without the types of what it holds — the prefabs, whose patches are differences of such
    // values. No text is written or read on the way.
    static Rain::Value save(const World &world);
    static bool load(World &world, const Rain::Value &value);
    static Rain::Value saveTree(World &world, Entity root);
    static Rain::Value saveEntity(Entity entity);
    static Rain::Value saveComponent(Entity entity, const ComponentType &type);
    static bool loadComponent(Entity entity, const ComponentType &type, const Rain::Value &value);
};

} // namespace Panda
