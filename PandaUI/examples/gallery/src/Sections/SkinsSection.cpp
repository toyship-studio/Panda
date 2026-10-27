#include "Sections/SkinsSection.hpp"

#include "Demo/DemoExample.hpp"
#include "Demo/DemoStyle.hpp"
#include "Demo/PageHeader.hpp"

#include <memory>
#include <string>
#include <vector>

namespace PandaUI::Gallery {

namespace {

    // The palette of the skin, as literals: a skin is colors of its own, not theme tokens.
    constexpr Color kIvory(0xF2E2B8FF);
    constexpr Color kIvoryDim(0xB8A684FF);
    constexpr Color kGold(0xC9A227FF);
    constexpr Color kGoldLight(0xF0D27AFF);
    constexpr Color kWood(0x4A2E18FF);
    constexpr Color kWoodDark(0x2A1A0EFF);
    constexpr Color kTransparent(0.f, 0.f, 0.f, 0.f);

    Font displayFont(const SkinAssets &assets, float size, FontWeight weight = FontWeight::Regular) {
        return Font(size, weight, assets.displayFont);
    }

    // ---- the appearances of the skin ----

    ButtonAppearance fantasyButton(const SkinAssets &assets) {
        ButtonAppearance a;
        const EdgeInsets caps(SkinAssets::PLATE_CAP, SkinAssets::PLATE_CAP, SkinAssets::PLATE_CAP, SkinAssets::PLATE_CAP);
        a.normal = {SurfaceStyle(kTransparent).withImage(SurfaceImage(assets.plateNormal, caps)), kIvory};
        a.hovered = {SurfaceStyle(kTransparent).withImage(SurfaceImage(assets.plateHovered, caps)), kGoldLight};
        a.pressed = {SurfaceStyle(kTransparent).withImage(SurfaceImage(assets.platePressed, caps)), kIvoryDim};
        a.focused = a.hovered;
        a.disabled = {SurfaceStyle(kTransparent).withImage(SurfaceImage(assets.plateDisabled, caps)), kIvoryDim};
        a.font = displayFont(assets, 17.f, FontWeight::Bold);
        a.padding = EdgeInsets(8.f, 22.f, 22.f, 8.f);
        a.minHeight = 40.f;
        a.cornerRadius = CornerRadii(0.f); // the plate has its own shape
        a.borderWidth = 0.f;
        a.titleShadow = TextShadow(Point(0.f, 1.f), Color(0.f, 0.f, 0.f, 0.6f));
        return a;
    }

    ToggleAppearance fantasyToggle(const SkinAssets &assets) {
        ToggleAppearance a;
        const EdgeInsets caps(SkinAssets::TRACK_CAP, SkinAssets::TRACK_CAP, SkinAssets::TRACK_CAP, SkinAssets::TRACK_CAP);
        a.trackOn = kGold;
        a.trackOff = kWoodDark;
        a.trackDisabled = kWood;
        a.trackOffImage = SurfaceImage(assets.track, caps);
        a.knob = kIvory;
        a.knobDisabled = kIvoryDim;
        a.focusBorder = kGoldLight;
        a.trackWidth = 48.f;
        a.trackHeight = 26.f;
        a.knobSize = 20.f;
        a.knobInset = 3.f;
        a.borderWidth = 0.f;
        return a;
    }

    SliderAppearance fantasySlider(const SkinAssets &assets) {
        SliderAppearance a;
        const EdgeInsets caps(SkinAssets::TRACK_CAP, SkinAssets::TRACK_CAP, SkinAssets::TRACK_CAP, SkinAssets::TRACK_CAP);
        a.track = kWoodDark;
        a.trackImage = SurfaceImage(assets.track, caps);
        a.fill = kGold;
        a.fillImage = SurfaceImage(assets.goldFill);
        a.fillDisabled = kWood;
        a.thumb = kIvory;
        a.thumbDisabled = kIvoryDim;
        a.thumbBorder = kWoodDark;
        a.thumbBorderHovered = kGoldLight;
        a.focusHalo = Color(0xC9A22755);
        a.thumbShadow = ShadowStyle{Color(0.f, 0.f, 0.f, 0.45f), Point(0.f, 2.f), 3.f};
        a.height = 30.f;
        a.trackHeight = 10.f;
        a.thumbSize = 22.f;
        a.thumbBorderWidth = 2.f;
        return a;
    }

    ProgressBarAppearance fantasyProgress(const SkinAssets &assets) {
        ProgressBarAppearance a;
        const EdgeInsets caps(SkinAssets::TRACK_CAP, SkinAssets::TRACK_CAP, SkinAssets::TRACK_CAP, SkinAssets::TRACK_CAP);
        a.track = kWoodDark;
        a.trackImage = SurfaceImage(assets.track, caps);
        a.fill = kGold;
        a.fillImage = SurfaceImage(assets.goldFill);
        a.height = 14.f;
        a.cornerRadius = 5.f;
        return a;
    }

    TextFieldAppearance fantasyTextField(const SkinAssets &assets) {
        TextFieldAppearance a;
        a.normal = SurfaceStyle(kWoodDark, kWood);
        a.hovered = SurfaceStyle(kWoodDark, ThemeColor(kGold).withShade(-0.3f));
        a.focused = SurfaceStyle(kWoodDark, kGold);
        a.disabled = SurfaceStyle(kWood, kWood);
        a.text = kIvory;
        a.placeholder = kIvoryDim;
        a.caret = kGoldLight;
        a.selection = Color(0xC9A22766);
        a.font = displayFont(assets, 16.f);
        a.contentInsets = EdgeInsets(6.f, 12.f, 12.f, 6.f);
        a.minHeight = 36.f;
        a.cornerRadius = CornerRadii(6.f);
        a.borderWidth = 2.f;
        return a;
    }

    SegmentedControlAppearance fantasySegmented(const SkinAssets &assets) {
        SegmentedControlAppearance a;
        const EdgeInsets caps(SkinAssets::TRACK_CAP, SkinAssets::TRACK_CAP, SkinAssets::TRACK_CAP, SkinAssets::TRACK_CAP);
        a.track = kWoodDark;
        a.trackImage = SurfaceImage(assets.track, caps);
        a.trackFocusBorder = kGold;
        a.segmentSelected = kGold;
        a.segmentHovered = kWood;
        a.segmentSelectedDisabled = kWood;
        a.textSelected = kWoodDark;
        a.textNormal = kIvoryDim;
        a.textHovered = kIvory;
        a.textDisabled = kIvoryDim;
        a.font = displayFont(assets, 16.f, FontWeight::Bold);
        a.height = 36.f;
        // Concentric with the groove image (its corners are 12 px): the track radius matches the
        // image, the selected plate sits one padding inside.
        a.trackRadius = 12.f;
        a.trackPadding = 4.f;
        a.segmentRadius = a.trackRadius - a.trackPadding;
        return a;
    }

    ListViewAppearance fantasyList(const SkinAssets &assets) {
        ListViewAppearance a;
        a.rowHeight = 34.f;
        a.font = displayFont(assets, 16.f);
        a.text = kIvory;
        a.selection = Color(0xC9A22766);
        a.selectionInactive = Color(0xC9A22733);
        a.hover = Color(0xF2E2B81A);
        return a;
    }

    // ---- the columns ----

    std::shared_ptr<Label> caption(const std::string &text, Font font, ThemeColor color) {
        auto label = std::make_shared<Label>(text);
        label->setFont(font);
        label->setTextColor(color);
        return label;
    }

    std::shared_ptr<View> row(float gap = Style::ContentGap) {
        auto panel = std::make_shared<Panel>();
        panel->setBackgroundColor(kTransparent);
        panel->layout().setFlexDirection(FlexDirection::Row);
        panel->layout().setAlignItems(Align::Center);
        panel->layout().setGap(gap);
        return panel;
    }

    std::shared_ptr<ListView> sampleList(float height) {
        auto list = std::make_shared<ListView>();
        for (const char *item : {"New Game", "Continue", "Load", "Options", "Credits", "Quit"}) { list->addItem(item); }
        list->setSelectedIndex(1);
        list->layout().setHeight(Length::points(height));
        return list;
    }

} // namespace

SkinsSection::SkinsSection(SkinAssets assets)
    : m_assets(std::move(assets)) {
    addSubviews();
    setupLayout();
    setupStyle();
}

std::shared_ptr<View> SkinsSection::makeEditorColumn() const {
    auto card = std::make_shared<Panel>();
    card->setBackgroundColor(ThemeColor::surface);
    card->surface().setCornerRadius(Style::CardRadius);
    card->surface().setBorderWidth(1.f);
    card->surface().setBorderColor(ThemeColor::border);
    card->layout().setFlexDirection(FlexDirection::Column);
    card->layout().setFlexGrow(1.f);
    card->layout().setFlexBasis(0.f);
    card->layout().setPadding(Style::CardPadding);
    card->layout().setGap(Style::ContentGap);

    card->addSubview(caption("Editor", Font(24.f, FontWeight::Bold), ThemeColor::textPrimary));
    card->addSubview(caption("The default appearances: the look of the editor.", Font(14.f), ThemeColor::textMuted));

    auto buttons = row();
    auto play = std::make_shared<Button>("Play");
    play->setStyle(ButtonStyle::Accent);
    buttons->addSubview(play);
    buttons->addSubview(std::make_shared<Button>("Options"));
    auto disabled = std::make_shared<Button>("Locked");
    disabled->setEnabled(false);
    buttons->addSubview(disabled);
    card->addSubview(buttons);

    auto controls = row();
    auto toggle = std::make_shared<Toggle>();
    toggle->setOn(true);
    controls->addSubview(toggle);
    auto slider = std::make_shared<Slider>();
    slider->setValue(0.6f);
    slider->layout().setFlexGrow(1.f);
    controls->addSubview(slider);
    card->addSubview(controls);

    auto progress = std::make_shared<ProgressBar>();
    progress->setProgress(0.72f);
    card->addSubview(progress);

    auto field = std::make_shared<TextField>();
    field->setPlaceholder("Hero name");
    card->addSubview(field);

    auto segmented = std::make_shared<SegmentedControl>();
    segmented->setSegments({"Easy", "Normal", "Hard"});
    segmented->setSelectedIndex(1);
    card->addSubview(segmented);

    auto listFrame = std::make_shared<Panel>();
    listFrame->setBackgroundColor(ThemeColor::input);
    listFrame->surface().setCornerRadius(Radius::lg);
    listFrame->surface().setBorderWidth(1.f);
    listFrame->surface().setBorderColor(ThemeColor::borderMed);
    listFrame->setClipsToBounds(true); // the rounded clip: the rows end at the rounded corners
    listFrame->layout().setFlexDirection(FlexDirection::Column);
    listFrame->addSubview(sampleList(4.5f * ListView::kRowHeight));
    card->addSubview(listFrame);
    return card;
}

std::shared_ptr<View> SkinsSection::makeFantasyColumn() const {
    const SkinAssets &assets = m_assets;
    auto card = std::make_shared<Panel>();
    const float cap = SkinAssets::FRAME_CAP;
    // The frame image carries its own fill and rounded corners; a color under it would show at the corners.
    card->setBackgroundColor(kTransparent);
    card->surface().setBackgroundImage(SurfaceImage(assets.frame, EdgeInsets(cap, cap, cap, cap)));
    card->layout().setFlexDirection(FlexDirection::Column);
    card->layout().setFlexGrow(1.f);
    card->layout().setFlexBasis(0.f);
    card->layout().setPadding(28.f);
    card->layout().setGap(Style::ContentGap);

    // The title: the display font, an outline and a drop shadow — HUD text over any background.
    auto title = caption("Fantasy", displayFont(assets, 28.f, FontWeight::Bold), kGoldLight);
    title->setTextOutline(TextOutline(1.5f, kWoodDark));
    title->setTextShadow(TextShadow(Point(0.f, 2.f), Color(0.f, 0.f, 0.f, 0.5f)));
    card->addSubview(title);
    card->addSubview(caption(
        assets.hasDisplayFont ? "Nine-slice plates, a serif face, text outlines and gradients — through the appearances."
                              : "Nine-slice plates, text outlines and gradients — through the appearances (no serif face installed).",
        displayFont(assets, 14.f), kIvoryDim
    ));

    const ButtonAppearance button = fantasyButton(assets);
    auto buttons = row();
    auto play = std::make_shared<Button>("Play");
    play->setAppearance(button);
    buttons->addSubview(play);
    auto options = std::make_shared<Button>("Options");
    options->setAppearance(button);
    buttons->addSubview(options);
    auto disabled = std::make_shared<Button>("Locked");
    disabled->setAppearance(button);
    disabled->setEnabled(false);
    buttons->addSubview(disabled);
    card->addSubview(buttons);

    auto controls = row();
    auto toggle = std::make_shared<Toggle>();
    toggle->setAppearance(fantasyToggle(assets));
    toggle->setOn(true);
    controls->addSubview(toggle);
    auto slider = std::make_shared<Slider>();
    slider->setAppearance(fantasySlider(assets));
    slider->setValue(0.6f);
    slider->layout().setFlexGrow(1.f);
    controls->addSubview(slider);
    card->addSubview(controls);

    auto progress = std::make_shared<ProgressBar>();
    progress->setAppearance(fantasyProgress(assets));
    progress->setProgress(0.72f);
    card->addSubview(progress);

    auto field = std::make_shared<TextField>();
    field->setAppearance(fantasyTextField(assets));
    field->setPlaceholder("Hero name");
    card->addSubview(field);

    auto segmented = std::make_shared<SegmentedControl>();
    segmented->setAppearance(fantasySegmented(assets));
    segmented->setSegments({"Easy", "Normal", "Hard"});
    segmented->setSelectedIndex(1);
    card->addSubview(segmented);

    // A gradient plate with a rounded clip: the list inside ends at the rounded corners.
    auto listFrame = std::make_shared<Panel>();
    listFrame->surface().setBackgroundGradient(SurfaceGradient(kWood, kWoodDark, GradientDirection::TopToBottom));
    listFrame->surface().setCornerRadii(CornerRadii(16.f, 4.f, 16.f, 4.f)); // differing radii: the tessellated path
    listFrame->surface().setBorderWidth(2.f);
    listFrame->surface().setBorderColor(kGold);
    listFrame->setClipsToBounds(true);
    listFrame->layout().setFlexDirection(FlexDirection::Column);
    const ListViewAppearance listLook = fantasyList(assets);
    auto list = sampleList(4.5f * listLook.rowHeight);
    list->setAppearance(listLook);
    listFrame->addSubview(list);
    card->addSubview(listFrame);
    return card;
}

void SkinsSection::addSubviews() {
    addSubview(std::make_shared<PageHeader>(
        "Skins", "The same controls with the editor look and with a game skin: Button::setAppearance and the "
                 "appearances of the other widgets, nine-slice images in Surface, text outlines and shadows, gradients, "
                 "per-corner radii and rounded clipping. Widget::defaultAppearance() restyles a whole game at start."
    ));
    auto columns = std::make_shared<Panel>();
    columns->setBackgroundColor(kTransparent);
    columns->layout().setFlexDirection(FlexDirection::Row);
    columns->layout().setAlignItems(Align::Stretch);
    columns->layout().setGap(Style::PageGap);
    columns->addSubview(makeEditorColumn());
    columns->addSubview(makeFantasyColumn());
    addSubview(columns);
}

void SkinsSection::setupLayout() {
    layout().setFlexDirection(FlexDirection::Column);
    layout().setGap(Style::PageGap);
}

void SkinsSection::setupStyle() {
    setBackgroundColor(Color(0x00000000));
}

} // namespace PandaUI::Gallery
