#include "Panda/AudioEditor/AudioEditorExtensions.hpp"

#include "Panda/AudioEditor/AudioClipEditor.hpp"
#include "Panda/AudioEditor/AudioComponentSections.hpp"
#include "Panda/AudioEditor/AudioEditorSettings.hpp"

#include <Panda/Audio/AudioEngine.hpp>
#include <Panda/Extensions/Extensions.hpp>
#include <Panda/GameLogic/GameContext.hpp>
#include <PandaEditorKit/EditorExtensions.hpp>
#include <PandaEditorKit/EditorHost.hpp>
#include <PandaEditorKit/SettingsCommands.hpp>
#include <PandaEditorKit/Widgets/FormWidgets.hpp>
#include <PandaEditorKit/Widgets/Icons.hpp>

#include <memory>

namespace AudioEditor {

using namespace Editor;
using namespace PandaUI;

namespace {

    std::unique_ptr<AssetEditor> createClipEditor(const AssetEditorContext &context) {
        return std::make_unique<AudioClipEditor>(context);
    }

    // ---- The AUDIO section of the editor settings.

    void buildAudioSettings(CollapsibleSection &section, Panda::TypeContext &store) {
        auto data = std::make_shared<Panda::AudioEditorSettings>(store.get<Panda::AudioEditorSettings>());
        auto emit = [data] { commitSettings<Panda::AudioEditorSettings>(Panda::SettingsScope::Editor, *data); };
        addToggleRow(section, "Mute", data->mute, [data, emit](bool on) {
            data->mute = on;
            emit();
        });
        section.addContent(makeFormHint("Silences everything the editor plays: clip previews and Play."));
    }

    // The mute of the editor settings reaches the mixer every frame (an application system of the
    // editor part): the value is read where it is used, as the sections of the other modules do.
    void applyEditorSettings(double) {
        Panda::Audio::AudioEngine *engine = Panda::Audio::AudioEngine::instance();
        if (engine == nullptr) { return; }
        const bool mute = editorHost().settings(Panda::SettingsScope::Editor).get<Panda::AudioEditorSettings>().mute;
        if (engine->isMuted() != mute) { engine->setMuted(mute); }
    }

    // ---- Statistics.

    void collectStatistics(uint32_t *values) {
        Panda::Audio::AudioEngine *engine = Panda::Audio::AudioEngine::instance();
        const Panda::Audio::AudioEngineStats stats = engine ? engine->stats() : Panda::Audio::AudioEngineStats{};
        values[0] = stats.activeVoices;
        values[1] = stats.cachedClips;
        values[2] = stats.sampleRate;
    }

} // namespace

void registerExtensions(EditorExtensions &extensions) {
    using namespace Panda;
    // The editor-scope section of the module: registered with the settings registry like the
    // sections of the editor itself; the editor adopts it from the loaded settings.json.
    GameContext::getExtensions().settings.add(settingsType<AudioEditorSettings>(SettingsScope::Editor, "audio"));
    AppSystem editorSystem;
    editorSystem.name = "audio.editor";
    editorSystem.tick = applyEditorSettings;
    GameContext::getExtensions().appSystems.add(std::move(editorSystem));

    extensions.assetKinds.add({"audio", Ic::Audio, PandaUI::Color(0xD9A441FF), createClipEditor});
    registerComponentSections(extensions);
    extensions.settingsSections.push_back({SettingsScope::Editor, "AUDIO", buildAudioSettings});
    extensions.statistics.push_back({"AUDIO", {"Voices", "Clips", "Sample Rate"}, collectStatistics});
}

} // namespace AudioEditor
