#include "Panda/ParticlesEditor/ParticlesEditorExtensions.hpp"

#include <PandaEditorKit/ComponentCommands.hpp>
#include <PandaEditorKit/EditorExtensions.hpp>
#include <PandaEditorKit/Widgets/FormWidgets.hpp>
#include <PandaEditorKit/Widgets/Icons.hpp>

#include <Panda/GameLogic/World.hpp>
#include <Panda/Particles/ParticleComponents.hpp>

#include <memory>
#include <unordered_set>

namespace ParticlesEditor {

using namespace Editor;
using namespace PandaUI;

namespace {

    void buildEmitterSection(CollapsibleSection &section, const ComponentInspectorContext &context) {
        Panda::Entity primary = context.entities.front();
        auto data = std::make_shared<Panda::ParticleEmitterComponent>(primary.getComponent<Panda::ParticleEmitterComponent>());
        Panda::World *world = &context.world;
        const std::vector<Panda::Entity> entities = context.entities;
        auto emit = [world, entities, data] { commitComponent<Panda::ParticleEmitterComponent>(*world, entities, *data); };

        addToggleRow(section, "Playing", data->playing, [data, emit](bool v) { data->playing = v; emit(); });
        addNumberRow(section, "Rate (1/s)", 0.f, 5000.f, data->rate, [data, emit](float v) { data->rate = v; emit(); });
        addNumberRow(section, "Lifetime (s)", 0.05f, 60.f, data->lifetime, [data, emit](float v) { data->lifetime = v; emit(); });
        addNumberRow(section, "Lifetime Variation", 0.f, 1.f, data->lifetimeVariation, [data, emit](float v) {
            data->lifetimeVariation = v;
            emit();
        });
        addNumberRow(section, "Speed (m/s)", 0.f, 100.f, data->speed, [data, emit](float v) { data->speed = v; emit(); });
        addNumberRow(section, "Speed Variation", 0.f, 1.f, data->speedVariation, [data, emit](float v) {
            data->speedVariation = v;
            emit();
        });
        addNumberRow(section, "Spread (deg)", 0.f, 180.f, data->spreadDegrees, [data, emit](float v) {
            data->spreadDegrees = v;
            emit();
        });
        addNumberRow(section, "Gravity X", -100.f, 100.f, data->gravity.x, [data, emit](float v) { data->gravity.x = v; emit(); });
        addNumberRow(section, "Gravity Y", -100.f, 100.f, data->gravity.y, [data, emit](float v) { data->gravity.y = v; emit(); });
        addNumberRow(section, "Gravity Z", -100.f, 100.f, data->gravity.z, [data, emit](float v) { data->gravity.z = v; emit(); });
        addNumberRow(section, "Start Size (m)", 0.001f, 50.f, data->startSize, [data, emit](float v) { data->startSize = v; emit(); });
        addNumberRow(section, "End Size (m)", 0.f, 50.f, data->endSize, [data, emit](float v) { data->endSize = v; emit(); });
        addColorRow(section, "Start Color", data->startColor, [data, emit](const Panda::Color &c) { data->startColor = c; emit(); });
        addColorRow(section, "End Color", data->endColor, [data, emit](const Panda::Color &c) { data->endColor = c; emit(); });
        addNumberRow(section, "Max Particles", 1.f, 100000.f, static_cast<float>(data->maxParticles), [data, emit](float v) {
            data->maxParticles = static_cast<uint32_t>(v);
            emit();
        });
        addAssetRow(section, "Texture", "texture", static_cast<uint32_t>(data->texture.getId()), [data, emit](uint32_t id) {
            data->texture = assetRefOf(id);
            emit();
        });
        addToggleRow(section, "World Space", data->worldSpace, [data, emit](bool v) { data->worldSpace = v; emit(); });
    }

    void setEmitterPresence(Panda::World &world, const std::vector<Panda::Entity> &entities, bool add) {
        toggleComponentPresence<Panda::ParticleEmitterComponent>(
            world, std::unordered_set<Panda::Entity>(entities.begin(), entities.end()), add
        );
    }

} // namespace

void registerExtensions(EditorExtensions &extensions) {
    ComponentInspector emitter;
    emitter.component = "particleEmitterComponent";
    emitter.displayName = "Particle Emitter";
    emitter.icon = Ic::Sprite;
    emitter.setPresence = setEmitterPresence;
    emitter.build = buildEmitterSection;
    extensions.componentInspectors.add(std::move(emitter));
}

} // namespace ParticlesEditor
