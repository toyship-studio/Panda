#pragma once

#include <Panda/Audio/AudioEngine.hpp>
#include <PandaEditorKit/EditorExtensions.hpp>
#include <PandaUI/PandaUI.hpp>

#include <memory>

namespace AudioEditor {

// The preview voice of a clip tab: shared between the form and its transport view, which polls
// the position every frame. Stopped when the tab goes away.
struct ClipPreview final {
    Panda::Audio::VoiceHandle voice;
    bool loop = false;

    bool isPlaying() const;
    void stop();
};

// Tab form of an audio clip (the AssetKindUi::createEditor registry): what the file declares and
// a transport to listen to it. A clip has nothing to edit, so there is no document and no dirty
// state; the preview plays through the mixer of the application, on the "ui" bus.
class AudioClipEditor final : public Editor::AssetEditor {
public:
    explicit AudioClipEditor(const Editor::AssetEditorContext &context);
    ~AudioClipEditor() override;
    void build(PandaUI::View &container) override;

private:
    Editor::AssetEditorContext m_context;
    std::shared_ptr<ClipPreview> m_preview;
};

} // namespace AudioEditor
