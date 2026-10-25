#pragma once

namespace Demo::Style {

// Shared styling constants of the demo (paddings, radii, font sizes).
// Colors come from PandaUI::theme(); they are intentionally absent here.
// The geometry follows PandaUI::Radius / PandaUI::Spacing (the GPUI scale): a page is xxl, a card xl.
constexpr float PagePadding = 24.f;
constexpr float PageGap = 16.f;
constexpr float CardRadius = 8.f;  // PandaUI::Radius::lg
constexpr float CardPadding = 16.f; // PandaUI::Spacing::xl
constexpr float ContentGap = 12.f;

constexpr float TitleFont = 24.f;       // page title (design page/about title)
constexpr float SubtitleFont = 13.f;    // subtitles/captions (design secondary 13)
constexpr float ExampleTitleFont = 18.f; // example block title (design dialog/example title)
constexpr float BodyFont = 16.f;        // main UI text (design base 16)

constexpr float SidebarWidth = 224.f;
constexpr float SidebarRowHeight = 30.f; // a list row

} // namespace Demo::Style
