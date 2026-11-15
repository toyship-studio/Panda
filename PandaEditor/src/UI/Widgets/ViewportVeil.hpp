#pragma once

#include <PandaUI/PandaUI.hpp>

#include <memory>
#include <string>

namespace Editor {

// The cover of a viewport that is on screen and not drawn. The Scene and the Game are not rendered
// together: the one the user works in is, and the other keeps its last frame under this veil — a
// light gray over the picture and a small white caption that says who has the focus. The veil
// takes no input: a click goes to the surface under it, which takes the focus and renders from
// the next frame.
class ViewportVeil final : public PandaUI::Panel {
public:
    explicit ViewportVeil(const std::string &caption);
    void setShown(bool shown);

private:
    std::shared_ptr<PandaUI::Panel> m_plate;
    std::shared_ptr<PandaUI::Label> m_caption;
};

} // namespace Editor
