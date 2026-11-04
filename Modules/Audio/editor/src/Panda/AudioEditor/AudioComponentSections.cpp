#include "Panda/AudioEditor/AudioComponentSections.hpp"

#include <Panda/Audio/AudioClipAsset.hpp>
#include <Panda/Audio/AudioComponents.hpp>
#include <Panda/Audio/AudioEngine.hpp>
#include <Panda/Audio/AudioSources.hpp>
#include <Panda/GameLogic/World.hpp>
#include <PandaEditorKit/ComponentCommands.hpp>
#include <PandaEditorKit/EditorExtensions.hpp>
#include <PandaEditorKit/Widgets/FormWidgets.hpp>
#include <PandaEditorKit/Widgets/Icons.hpp>

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace AudioEditor {

using namespace Editor;
using namespace PandaUI;
using Panda::Audio::AudioEngine;
using Panda::Audio::AudioSourceComponent;

namespace {

    const std::vector<std::string> ATTENUATION_NAMES = {"None", "Inverse", "Linear", "Exponential"};

    std::vector<std::string> busNames() {
        return std::vector<std::string>(AudioEngine::BUS_NAMES.begin(), AudioEngine::BUS_NAMES.end());
    }

    int busIndex(const std::string &bus) {
        for (size_t i = 0; i < AudioEngine::BUS_NAMES.size(); ++i) {
            if (bus == AudioEngine::BUS_NAMES[i]) { return static_cast<int>(i); }
        }
        return 2; // sfx
    }

    // The preview of a source outside Play: one voice of the editor at a time, through the mixer,
    // not spatial (there is no listener to hear it from).
    Panda::Audio::VoiceHandle s_preview;

    void stopPreview() {
        if (AudioEngine *engine = AudioEngine::instance()) { engine->stop(s_preview); }
        s_preview = {};
    }

    void playPreview(const AudioSourceComponent &source) {
        AudioEngine *engine = AudioEngine::instance();
        if (engine == nullptr) { return; }
        stopPreview();
        Panda::Audio::AudioClipAsset *clip = source.clip.as<Panda::Audio::AudioClipAsset>().get();
        if (clip == nullptr) { return; }
        Panda::Audio::VoiceParams params;
        params.bus = source.bus;
        params.volume = source.volume;
        params.pitch = source.pitch;
        params.loop = source.loop;
        s_preview = engine->play(*clip, params);
    }

    void buildSourceSection(CollapsibleSection &section, const ComponentInspectorContext &context) {
        Panda::Entity primary = context.entities.front();
        auto data = std::make_shared<AudioSourceComponent>(primary.getComponent<AudioSourceComponent>());
        Panda::World *world = &context.world;
        const std::vector<Panda::Entity> entities = context.entities;
        auto emit = [world, entities, data] { commitComponent<AudioSourceComponent>(*world, entities, *data); };

        addAssetRow(section, "Clip", "audio", static_cast<uint32_t>(data->clip.getId()), [data, emit](uint32_t id) {
            data->clip = assetRefOf(id);
            emit();
        });
        addComboRow(section, "Bus", busNames(), busIndex(data->bus), [data, emit](int index) {
            if (index >= 0 && index < static_cast<int>(AudioEngine::BUS_NAMES.size())) {
                data->bus = AudioEngine::BUS_NAMES[static_cast<size_t>(index)];
                emit();
            }
        });
        addNumberRow(section, "Volume", 0.f, 1.f, data->volume, [data, emit](float v) {
            data->volume = v;
            emit();
        });
        addNumberRow(section, "Pitch", 0.1f, 3.f, data->pitch, [data, emit](float v) {
            data->pitch = v;
            emit();
        });
        addToggleRow(section, "Loop", data->loop, [data, emit](bool v) {
            data->loop = v;
            emit();
        });
        addToggleRow(section, "Play On Start", data->playOnStart, [data, emit](bool v) {
            data->playOnStart = v;
            emit();
        });
        addToggleRow(section, "Spatial", data->spatial, [data, emit](bool v) {
            data->spatial = v;
            emit();
        });
        if (data->spatial) {
            addNumberRow(section, "Min Distance (m)", 0.f, 1000.f, data->minDistance, [data, emit](float v) {
                data->minDistance = v;
                emit();
            });
            addNumberRow(section, "Max Distance (m)", 0.f, 10000.f, data->maxDistance, [data, emit](float v) {
                data->maxDistance = v;
                emit();
            });
            addComboRow(
                section, "Attenuation", ATTENUATION_NAMES, static_cast<int>(data->attenuation),
                [data, emit](int index) {
                    if (index >= 0 && index <= static_cast<int>(Panda::Audio::AttenuationModel::Exponential)) {
                        data->attenuation = static_cast<Panda::Audio::AttenuationModel>(index);
                        emit();
                    }
                }
            );
            addNumberRow(section, "Doppler", 0.f, 5.f, data->dopplerFactor, [data, emit](float v) {
                data->dopplerFactor = v;
                emit();
            });
        }

        // Play / Stop: in a running world the live voice of the entity (what a script would do),
        // otherwise a preview through the mixer.
        auto play = makeFormButton("Play", ButtonStyle::Accent, [world, primary, data] {
            if (world->isRunning()) {
                Panda::Audio::Sources::play(*world, primary);
            } else {
                playPreview(*data);
            }
        });
        auto stop = makeFormButton("Stop", ButtonStyle::Neutral, [world, primary] {
            if (world->isRunning()) {
                Panda::Audio::Sources::stop(*world, primary);
            } else {
                stopPreview();
            }
        });
        section.addContent(makeFormButtonRow({play, stop}));
    }

    void setSourcePresence(Panda::World &world, const std::vector<Panda::Entity> &entities, bool add) {
        toggleComponentPresence<AudioSourceComponent>(
            world, std::unordered_set<Panda::Entity>(entities.begin(), entities.end()), add
        );
    }

    void buildListenerSection(CollapsibleSection &section, const ComponentInspectorContext &) {
        section.addContent(makeFormHint(
            "Spatial sounds are heard from this entity: its position, forward (−Z) and up (+Y). "
            "Without a listener the main camera is used."
        ));
    }

    void setListenerPresence(Panda::World &world, const std::vector<Panda::Entity> &entities, bool add) {
        toggleComponentPresence<Panda::Audio::AudioListenerComponent>(
            world, std::unordered_set<Panda::Entity>(entities.begin(), entities.end()), add
        );
    }

} // namespace

void registerComponentSections(EditorExtensions &extensions) {
    ComponentInspector source;
    source.component = "audioSourceComponent";
    source.displayName = "Audio Source";
    source.icon = Ic::Audio;
    source.setPresence = setSourcePresence;
    source.build = buildSourceSection;
    extensions.componentInspectors.add(std::move(source));

    ComponentInspector listener;
    listener.component = "audioListenerComponent";
    listener.displayName = "Audio Listener";
    listener.icon = Ic::Audio;
    listener.setPresence = setListenerPresence;
    listener.build = buildListenerSection;
    extensions.componentInspectors.add(std::move(listener));
}

} // namespace AudioEditor
