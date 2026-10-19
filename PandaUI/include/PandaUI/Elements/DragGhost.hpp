#pragma once

#include "PandaUI/Core/View.hpp"

#include <memory>
#include <string>

namespace PandaUI {

class Panel;

// Drag "ghost": a small caption bubble that follows the cursor during a drag.
// A passive overlay (does not catch input). The position is set by Context on every movement.
class DragGhost : public View {
public:
    explicit DragGhost(std::string text);

    void setCursorPosition(Point position);

private:
    std::shared_ptr<Panel> m_bubble;
};

} // namespace PandaUI
