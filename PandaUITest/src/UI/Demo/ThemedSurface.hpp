#pragma once

#include "UI/Demo/DemoComponent.hpp"

namespace Demo {

enum class SurfaceRole {
    Surface,
    SurfaceVariant,
    Control,
};

// A container with a theme background chosen by role. Unlike a bare Panel, it re-reads the color on
// a theme change (through DemoComponent::themeChanged → setupStyle). The layout is set up by the caller.
class ThemedSurface final : public DemoComponent {
public:
    explicit ThemedSurface(SurfaceRole role, float cornerRadius = 0.f);

private:
    void setupStyle() override;

    SurfaceRole m_role;
    float m_cornerRadius;
};

} // namespace Demo
