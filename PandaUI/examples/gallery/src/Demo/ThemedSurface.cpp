#include "Demo/ThemedSurface.hpp"

namespace PandaUI::Gallery {

ThemedSurface::ThemedSurface(SurfaceRole role, float cornerRadius)
    : m_role(role)
    , m_cornerRadius(cornerRadius) {
    setupStyle();
}

void ThemedSurface::setupStyle() {
    const Theme &t = Theme::current();
    Color background = t.surface;
    switch (m_role) {
        case SurfaceRole::Surface:
            background = t.surface;
            break;
        case SurfaceRole::SurfaceVariant:
            background = t.surfaceVariant;
            break;
        case SurfaceRole::Control:
            background = t.control;
            break;
    }
    setBackgroundColor(background);
    surface().setCornerRadius(m_cornerRadius);
}

} // namespace PandaUI::Gallery
