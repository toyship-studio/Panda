#pragma once

#include <PandaUI/PandaUI.hpp>

#include <memory>
#include <string>

namespace Editor {

// The surface of the Game tab: the frame of the game as the game shows it — the main camera, the
// game UI on top, no editor layer (outline, gizmo, helpers). It catches the focus so that the keys
// go to the game, and consumes no pointer event: clicks reach Panda::Input, where the scripts read
// them. The engine feeds the texture of the Game surface (EditorViewports) and tells whether this
// surface renders this frame.
class GameViewportView final : public PandaUI::Panel {
public:
    GameViewportView();

    // The output texture of the Game surface; flipVertical for bottom-left-origin render targets.
    void setTexture(PandaUI::TextureHandle texture, bool flipVertical);
    // A caption over the frame ("No main camera in the world"); empty — none.
    void setMessage(const std::string &text);
    // false — the Scene renders this frame and the Game keeps its last one: the badge says so.
    void setLive(bool live);

protected:
    bool pointerDown(PandaUI::PointerEvent &event) override;

private:
    std::shared_ptr<PandaUI::ImageView> m_image;
    std::shared_ptr<PandaUI::Label> m_message;
    std::shared_ptr<PandaUI::Panel> m_badge;
    std::shared_ptr<PandaUI::Label> m_badgeLabel;
};

} // namespace Editor
