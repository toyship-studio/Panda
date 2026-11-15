#pragma once

#include "Panda/Extensions/ComponentTypes.hpp"
#include "Panda/GameLogic/World.hpp"

#include <Rain/Rain.hpp>

#include <istream>
#include <ostream>

namespace Panda {

// World ↔ stream (the editor's .world is JSON, the export is binary; any coder). The settings
// of the world scope go through the settings registry (SettingsMapper: the core's and the
// modules' sections alike); entities — by the component type registry (Extensions::components):
// key = type name, the value is written and read by the contribution; a key without a
// contribution (the module is not loaded) is kept as is (ForeignComponents, ForeignSettings) and
// written back — by the JSON coder; the binary one carries only registered data. The format —
// PandaSDK/docs/world-format.md.
class WorldMapper {
public:
    static void save(const World &world, Rain::Encoder &encoder, std::ostream &stream);
    // Clears the world and fills it from the stream. false — the stream was not parsed; on a
    // parse error the world is untouched, a content error leaves a partially filled world.
    static bool load(World &world, Rain::Decoder &decoder, std::istream &stream);
    // A fragment of a world: the given entities with everything under them, in the format of the
    // world file without the settings sections — `{"entities": [...]}`. The text form of a
    // selection: the clipboard of the editor, a snippet an agent writes.
    static void saveEntities(
        World &world, const std::vector<Entity> &entities, Rain::Encoder &encoder, std::ostream &stream
    );
    // Adds the entities of a fragment (or of a whole world file) to the world as new roots with
    // new ids; `added` gets the roots. false — the stream was not parsed, the world is untouched.
    static bool loadEntities(
        World &world, Rain::Decoder &decoder, std::istream &stream, std::vector<Entity> &added
    );
    // One component of an entity in the form the world file holds it — an object with the name
    // of the type as its key: `{"pointLightComponent": {...}}`. The text form of a component:
    // the clipboard of the inspector, a snippet an agent writes.
    static void saveComponent(Entity entity, const ComponentType &type, Rain::Encoder &encoder, std::ostream &stream);
    // The value of the component from such a text into the entity: the component it has is
    // replaced, one it does not have is added. false — the stream was not parsed or does not
    // hold this component; the entity is untouched then.
    static bool loadComponent(Entity entity, const ComponentType &type, Rain::Decoder &decoder, std::istream &stream);
};

} // namespace Panda
