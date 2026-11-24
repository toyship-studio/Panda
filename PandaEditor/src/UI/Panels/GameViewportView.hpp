#pragma once

#include <PandaUI/PandaUI.hpp>

#include <memory>
#include <string>

namespace Editor {

// The surface of the Game, the middle of the world tab in the place of the World view while it
// shows the game: the frame of the game as the game shows it — the main camera, the game UI on
// top, no editor layer (outline, gizmo, helpers). It catches the focus so that the keys go to the
// game, and consumes no pointer event: clicks reach Panda::Input, where the scripts read them.
// The engine feeds the texture of the Game surface (EditorViewports).
class GameViewportView final : public PandaUI::Panel {
public:
    GameViewportView();

    // The output texture of the Game surface; flipVertical for bottom-left-origin render targets.
    void setTexture(PandaUI::TextureHandle texture, bool flipVertical);
    // A caption over the frame ("No main camera in the world"); empty — none.
    void setMessage(const std::string &text);

protected:
    bool pointerDown(PandaUI::PointerEvent &event) override;

private:
    std::shared_ptr<PandaUI::ImageView> m_image;
    std::shared_ptr<PandaUI::Label> m_message;
};

} // namespace Editor
