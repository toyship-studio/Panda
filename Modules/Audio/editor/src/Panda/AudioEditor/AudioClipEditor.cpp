#include "Panda/AudioEditor/AudioClipEditor.hpp"

#include <Panda/Audio/AudioClipAsset.hpp>
#include <Panda/GameLogic/GameContext.hpp>
#include <PandaEditorKit/EditorHost.hpp>
#include <PandaEditorKit/Widgets/FormWidgets.hpp>
#include <PandaEditorKit/Widgets/Typography.hpp>

#include <cmath>
#include <cstdio>
#include <string>

namespace AudioEditor {

using namespace PandaUI;
using Panda::Audio::AudioClipAsset;
using Panda::Audio::AudioEngine;

namespace {

    std::string formatTime(float seconds) {
        const int whole = static_cast<int>(std::floor(seconds));
        const int tenths = static_cast<int>((seconds - static_cast<float>(whole)) * 10.f);
        char text[32];
        std::snprintf(text, sizeof(text), "%d:%02d.%d", whole / 60, whole % 60, tenths);
        return text;
    }

    std::string formatSize(size_t bytes) {
        char text[32];
        if (bytes >= 1024 * 1024) {
            std::snprintf(text, sizeof(text), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
        } else {
            std::snprintf(text, sizeof(text), "%.0f KB", static_cast<double>(bytes) / 1024.0);
        }
        return text;
    }

    std::string formatChannels(uint32_t channels) {
        switch (channels) {
            case 1:
                return "Mono";
            case 2:
                return "Stereo";
            default:
                return std::to_string(channels) + " channels";
        }
    }

    std::string upper(std::string text) {
        for (char &c : text) { c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }
        return text;
    }

    // The position of the preview, polled every frame: a progress bar and the time of the voice.
    class TransportView final : public Panel {
    public:
        TransportView(std::shared_ptr<ClipPreview> preview, float lengthSeconds)
            : m_preview(std::move(preview))
            , m_length(lengthSeconds) {
            setBackgroundColor(Color(0x00000000));
            layout().setFlexDirection(FlexDirection::Row);
            layout().setAlignItems(Align::Center);
            layout().setGap(10.f);
            layout().setMargin(Edge::Vertical, 4.f);
            m_progress = std::make_shared<ProgressBar>();
            m_progress->layout().setFlexGrow(1.f);
            m_progress->layout().setFlexBasis(0.f);
            addSubview(m_progress);
            m_time = std::make_shared<Label>(formatTime(0.f) + " / " + formatTime(m_length));
            m_time->setFont(Editor::Typo::ValueSmall);
            m_time->setTextRole(TextRole::Secondary);
            addSubview(m_time);
        }

        void update(double) override {
            AudioEngine *engine = AudioEngine::instance();
            const float cursor = engine && m_preview->isPlaying() ? engine->cursorSeconds(m_preview->voice) : 0.f;
            m_progress->setProgress(m_length > 0.f ? cursor / m_length : 0.f);
            m_time->setText(formatTime(cursor) + " / " + formatTime(m_length));
        }

    private:
        std::shared_ptr<ClipPreview> m_preview;
        std::shared_ptr<ProgressBar> m_progress;
        std::shared_ptr<Label> m_time;
        float m_length;
    };

} // namespace

bool ClipPreview::isPlaying() const {
    AudioEngine *engine = AudioEngine::instance();
    return engine != nullptr && engine->isValid(voice);
}

void ClipPreview::stop() {
    if (AudioEngine *engine = AudioEngine::instance()) { engine->stop(voice); }
    voice = {};
}

AudioClipEditor::AudioClipEditor(const Editor::AssetEditorContext &context)
    : m_context(context)
    , m_preview(std::make_shared<ClipPreview>()) {}

AudioClipEditor::~AudioClipEditor() {
    m_preview->stop();
}

void AudioClipEditor::build(View &container) {
    const Panda::AssetId id = m_context.asset;
    Panda::AssetRef<AudioClipAsset> clipRef{id};
    AudioClipAsset *clip = clipRef.get();

    auto info = Editor::makeFormSection("AUDIO CLIP");
    const auto addText = [&info](const char *title, std::string text, bool muted) {
        auto label = std::make_shared<Label>(std::move(text));
        label->setFont(Editor::Typo::Body);
        if (muted) { label->setTextRole(TextRole::Secondary); }
        info->addContent(std::make_shared<PropertyRow>(title, label));
    };
    if (clip == nullptr || clip->isEmpty()) {
        addText("Format", "Not a readable audio file", true);
        container.addSubview(info);
        return;
    }
    const Panda::Audio::AudioClipInfo &clipInfo = clip->info();
    const float length = clipInfo.durationSeconds();
    addText("Format", upper(clipInfo.format), false);
    addText("Channels", formatChannels(clipInfo.channels), false);
    addText("Sample Rate", std::to_string(clipInfo.sampleRate) + " Hz", false);
    addText("Duration", formatTime(length), false);
    addText("Size", formatSize(clip->bytes()->size()), true);
    addText(
        "Playback",
        length < AudioEngine::DECODE_IN_MEMORY_SECONDS ? "Decoded into memory once" : "Decoded while it plays",
        true
    );
    container.addSubview(info);

    // The transport: the preview is a voice of the application mixer on the "ui" bus, not spatial.
    auto preview = Editor::makeFormSection("PREVIEW");
    std::shared_ptr<ClipPreview> state = m_preview;
    auto play = Editor::makeFormButton("Play", ButtonStyle::Accent, [state, id] {
        AudioEngine *engine = AudioEngine::instance();
        if (engine == nullptr) { return; }
        Panda::AssetRef<AudioClipAsset> ref{id};
        AudioClipAsset *asset = ref.get();
        if (asset == nullptr) { return; }
        if (engine->isValid(state->voice) && engine->isPaused(state->voice)) {
            engine->setPaused(state->voice, false);
            return;
        }
        state->stop();
        Panda::Audio::VoiceParams params;
        params.bus = "ui";
        params.loop = state->loop;
        state->voice = engine->play(*asset, params);
    });
    auto pause = Editor::makeFormButton("Pause", ButtonStyle::Neutral, [state] {
        if (AudioEngine *engine = AudioEngine::instance()) { engine->setPaused(state->voice, true); }
    });
    auto stop = Editor::makeFormButton("Stop", ButtonStyle::Neutral, [state] { state->stop(); });
    preview->addContent(Editor::makeFormButtonRow({play, pause, stop}));
    preview->addContent(std::make_shared<TransportView>(state, length));
    Editor::addToggleRow(*preview, "Loop", state->loop, [state](bool on) {
        state->loop = on;
        if (AudioEngine *engine = AudioEngine::instance(); engine != nullptr && engine->isValid(state->voice)) {
            Panda::Audio::VoiceParams params;
            params.bus = "ui";
            params.loop = on;
            engine->apply(state->voice, params);
        }
    });
    if (AudioEngine *engine = AudioEngine::instance(); engine != nullptr && !engine->stats().deviceActive) {
        preview->addContent(Editor::makeFormHint("No playback device: the mixer runs silently on the null backend."));
    }
    container.addSubview(preview);
}

} // namespace AudioEditor
