#include "PandaEditorKit/ComponentCommands.hpp"

#include <Panda/Extensions/ComponentTypes.hpp>
#include <Panda/Serialization/WorldMapper.hpp>
#include <Rain/Coders/JsonDecoder.hpp>
#include <Rain/Coders/JsonEncoder.hpp>

#include <sstream>
#include <utility>

namespace Editor {

namespace {

    bool applyText(Panda::Entity entity, const Panda::ComponentType &type, const std::string &text) {
        Rain::JsonDecoder decoder;
        std::istringstream stream(text);
        if (!Panda::WorldMapper::loadComponent(entity, type, decoder, stream)) { return false; }
        entity.setWorldChanged();
        return true;
    }

    // An edit of a component through its text form: what each target held before, and the text
    // it holds after. The component is found by the name of its type, so the command serves a
    // type the editor core does not know (a module's).
    class ComponentTextUpdate final : public Panda::WorldCommand {
    public:
        ComponentTextUpdate(
            const std::vector<Panda::Entity> &targets, const Panda::ComponentType &type, std::string after
        )
            : m_type(&type)
            , m_after(std::move(after)) {
            for (Panda::Entity entity : targets) {
                if (!entity.isValid() || !entity.hasComponent(type.typeId)) { continue; }
                m_before.emplace_back(entity, componentText(entity, type));
            }
        }

        bool isEmpty() const {
            return m_before.empty();
        }
        bool execute() override {
            for (auto &[entity, before] : m_before) {
                if (!entity.isValid() || !applyText(entity, *m_type, m_after)) { return false; }
            }
            return true;
        }
        bool undo() override {
            for (auto &[entity, before] : m_before) {
                if (!entity.isValid() || !applyText(entity, *m_type, before)) { return false; }
            }
            return true;
        }

    private:
        const Panda::ComponentType *m_type; // lives in the registry of the engine for the whole process
        std::string m_after;
        std::vector<std::pair<Panda::Entity, std::string>> m_before;
    };

} // namespace

std::string componentText(Panda::Entity entity, const Panda::ComponentType &type) {
    Rain::JsonEncoder encoder(true);
    std::ostringstream text;
    Panda::WorldMapper::saveComponent(entity, type, encoder, text);
    return text.str();
}

bool commitComponentText(
    Panda::World &world, const std::vector<Panda::Entity> &targets, const Panda::ComponentType &type,
    const std::string &text
) {
    // The text is tried on a scratch entity first: a clipboard that holds something else must
    // not leave a half-written component behind.
    {
        Panda::World scratch;
        Panda::Entity probe = scratch.instantiateEntity();
        if (type.create != nullptr) { type.create(probe); }
        if (!applyText(probe, type, text)) { return false; }
    }
    ComponentTextUpdate command(targets, type, text);
    if (command.isEmpty()) { return false; }
    world.getCommandHistory().execute(command);
    world.setChanged();
    return true;
}

bool resetComponent(Panda::World &world, const std::vector<Panda::Entity> &targets, const Panda::ComponentType &type) {
    if (type.create == nullptr) { return false; }
    Panda::World scratch;
    Panda::Entity fresh = scratch.instantiateEntity();
    type.create(fresh);
    return commitComponentText(world, targets, type, componentText(fresh, type));
}

} // namespace Editor
