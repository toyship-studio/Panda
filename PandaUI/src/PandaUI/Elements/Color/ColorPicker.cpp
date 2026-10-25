#include "PandaUI/Elements/Color/ColorPicker.hpp"

#include "PandaUI/Core/Theme.hpp"
#include "PandaUI/Rendering/UIRenderer.hpp"
#include "PandaUI/Runtime/Context.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_set>
#include <utility>

namespace PandaUI {

namespace {

    constexpr float SV_WIDTH = 200.f;
    constexpr float SV_HEIGHT = 150.f;
    constexpr float HUE_WIDTH = 18.f;
    constexpr float GAP = 10.f;

    // System sampler (eyedropper) — shared by all pickers; the result arrives asynchronously,
    // so picker liveness is checked against the registry (the popup may have closed while picking).
    ColorPicker::SystemColorSampler s_systemSampler;
    std::unordered_set<ColorPicker *> s_alivePickers;

    // Eyedropper button: a drawn "crosshair" (ring + dot) — no dependency on font glyphs.
    class SamplerButton final : public Control {
    public:
        explicit SamplerButton(std::function<void()> onClick)
            : m_onClick(std::move(onClick)) {
            setCursor(Cursor::PointingHand);
            layout().setSize({28.f, 28.f});
            surface().setCornerRadius(Radius::sm);
            surface().setBorderWidth(1.f);
            applyStyle();
        }

    private:
        void performPrimaryAction() override {
            if (m_onClick) { m_onClick(); }
        }
        void controlStateChanged(ControlState, ControlState) override {
            applyStyle();
        }
        void themeChanged() override {
            applyStyle();
        }
        void applyStyle() {
            const Theme &t = theme();
            const bool hovered =
                hasControlState(ControlState::Hovered) || hasControlState(ControlState::Highlighted);
            setBackgroundColor(t.input);
            surface().setBorderColor(hovered ? t.borderStrong : t.borderMed);
            setNeedsDisplay();
        }
        void draw(float offsetX, float offsetY) override {
            Context *context = getContext();
            if (!context) { return; }
            UIRenderer &renderer = context->getRenderer();
            const Rect frame = getFrame();
            const float cx = offsetX + frame.origin.x + frame.size.width * 0.5f;
            const float cy = offsetY + frame.origin.y + frame.size.height * 0.5f;
            const Color color = theme().textPrimary;
            UIRenderer::RectData ring;
            ring.center = {cx, cy, 0.f};
            ring.size = {13.f, 13.f};
            ring.color = color;
            renderer.drawRoundedRectBorder(ring, 6.5f, 1.6f);
            UIRenderer::RectData dot;
            dot.center = {cx, cy, 0.f};
            dot.size = {4.f, 4.f};
            dot.color = color;
            renderer.drawRoundedRectBorder(dot, 2.f, 2.f);
        }

        std::function<void()> m_onClick;
    };

    Color hsvToRgb(float h, float s, float v) {
        h = std::clamp(h, 0.f, 1.f) * 6.f;
        const float c = v * s;
        const float x = c * (1.f - std::fabs(std::fmod(h, 2.f) - 1.f));
        const float m = v - c;
        float r = 0.f;
        float g = 0.f;
        float b = 0.f;
        switch (static_cast<int>(h) % 6) {
            case 0: r = c; g = x; b = 0.f; break;
            case 1: r = x; g = c; b = 0.f; break;
            case 2: r = 0.f; g = c; b = x; break;
            case 3: r = 0.f; g = x; b = c; break;
            case 4: r = x; g = 0.f; b = c; break;
            default: r = c; g = 0.f; b = x; break;
        }
        return Color(r + m, g + m, b + m, 1.f);
    }

    void rgbToHsv(Color color, float &h, float &s, float &v) {
        const float mx = std::max({color.r, color.g, color.b});
        const float mn = std::min({color.r, color.g, color.b});
        const float d = mx - mn;
        v = mx;
        s = (mx <= 0.f) ? 0.f : d / mx;
        if (d <= 0.f) {
            h = 0.f;
            return;
        }
        float hue;
        if (mx == color.r) {
            hue = std::fmod((color.g - color.b) / d, 6.f);
        } else if (mx == color.g) {
            hue = (color.b - color.r) / d + 2.f;
        } else {
            hue = (color.r - color.g) / d + 4.f;
        }
        hue /= 6.f;
        if (hue < 0.f) { hue += 1.f; }
        h = hue;
    }

    std::string hexFromColor(Color color) {
        auto channel = [](float value) { return static_cast<int>(std::round(std::clamp(value, 0.f, 1.f) * 255.f)); };
        char buffer[8];
        std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", channel(color.r), channel(color.g), channel(color.b));
        return buffer;
    }

    bool parseHex(const std::string &text, Color &out) {
        std::string hex = text;
        if (!hex.empty() && hex.front() == '#') { hex.erase(hex.begin()); }
        if (hex.size() != 6) { return false; }
        for (char ch : hex) {
            if (!std::isxdigit(static_cast<unsigned char>(ch))) { return false; }
        }
        auto pair = [&](int index) {
            return static_cast<float>(std::strtol(hex.substr(index, 2).c_str(), nullptr, 16)) / 255.f;
        };
        out = Color(pair(0), pair(2), pair(4), 1.f);
        return true;
    }

} // namespace

ColorPicker::ColorPicker()
    : m_reservation(std::make_shared<Panel>())
    , m_hexField(std::make_shared<TextField>())
    , m_swatch(std::make_shared<Panel>())
    , m_onColorChanged(nullptr)
    , m_hue(0.f)
    , m_saturation(1.f)
    , m_value(1.f)
    , m_dragTarget(DragTarget::None) {
    setBackgroundColor(Color(0x00000000));
    layout().setFlexDirection(FlexDirection::Column);
    layout().setGap(10.f);
    layout().setWidth(Length::points(SV_WIDTH + GAP + HUE_WIDTH));

    // The palette and hue are drawn right in draw(); this block only reserves space for them
    // without intercepting the pointer (the ColorPicker itself receives it).
    m_reservation->setUserInteractionEnabled(false);
    m_reservation->setBackgroundColor(Color(0x00000000));
    m_reservation->layout().setWidth(Length::percent(100.f));
    m_reservation->layout().setHeight(Length::points(SV_HEIGHT));
    addSubview(m_reservation);

    auto row = std::make_shared<Panel>();
    row->setBackgroundColor(Color(0x00000000));
    row->layout().setFlexDirection(FlexDirection::Row);
    row->layout().setAlignItems(Align::Center);
    row->layout().setGap(8.f);

    m_hexField->layout().setFlexGrow(1.f);
    m_hexField->setOnTextChanged([this](TextField &) { handleHexInput(); });
    m_hexField->setOnSubmit([this](TextField &) { applyColorChanged(true, false); });

    m_swatch->setUserInteractionEnabled(false);
    m_swatch->layout().setSize({44.f, 28.f});
    m_swatch->surface().setCornerRadius(Radius::sm);
    m_swatch->surface().setBorderWidth(1.f);

    row->addSubview(m_hexField);
    row->addSubview(m_swatch);
    if (s_systemSampler) {
        // Eyedropper: the system screen sampler; the result is applied only if the picker is still alive.
        row->addSubview(std::make_shared<SamplerButton>([this] {
            s_systemSampler([this](Color picked) {
                if (s_alivePickers.count(this) == 0) { return; }
                rgbToHsv(picked, m_hue, m_saturation, m_value);
                applyColorChanged(true, true);
            });
        }));
    }
    addSubview(row);

    m_swatch->surface().setBorderColor(theme().border);
    applyColorChanged(true, false);
    s_alivePickers.insert(this);
}

ColorPicker::~ColorPicker() {
    s_alivePickers.erase(this);
}

void ColorPicker::setSystemColorSampler(SystemColorSampler sampler) {
    s_systemSampler = std::move(sampler);
}

void ColorPicker::setColor(Color color) {
    rgbToHsv(color, m_hue, m_saturation, m_value);
    applyColorChanged(true, false);
}

Color ColorPicker::getColor() const {
    return hsvToRgb(m_hue, m_saturation, m_value);
}

void ColorPicker::setOnColorChanged(ColorChangedCallback callback) {
    m_onColorChanged = std::move(callback);
}

void ColorPicker::applyColorChanged(bool reformatHex, bool notify) {
    const Color color = getColor();
    m_swatch->setBackgroundColor(color);
    if (reformatHex) { m_hexField->setText(hexFromColor(color)); }
    setNeedsDisplay();
    if (notify && m_onColorChanged) { m_onColorChanged(*this); }
}

void ColorPicker::handleHexInput() {
    Color parsed;
    if (!parseHex(m_hexField->getText(), parsed)) { return; }
    rgbToHsv(parsed, m_hue, m_saturation, m_value);
    applyColorChanged(false, true); // no reformatting while typing
}

void ColorPicker::updateFromPointer(Point windowPosition) {
    const Rect worldFrame = getWorldFrame();
    const Point local(windowPosition.x - worldFrame.origin.x, windowPosition.y - worldFrame.origin.y);
    if (m_dragTarget == DragTarget::SaturationValue) {
        m_saturation = std::clamp(local.x / SV_WIDTH, 0.f, 1.f);
        m_value = std::clamp(1.f - local.y / SV_HEIGHT, 0.f, 1.f);
    } else if (m_dragTarget == DragTarget::Hue) {
        m_hue = std::clamp(local.y / SV_HEIGHT, 0.f, 1.f);
    }
    applyColorChanged(true, true);
}

bool ColorPicker::pointerDown(PointerEvent &event) {
    const Rect worldFrame = getWorldFrame();
    const Point local(event.position.x - worldFrame.origin.x, event.position.y - worldFrame.origin.y);

    if (local.x >= 0.f && local.x <= SV_WIDTH && local.y >= 0.f && local.y <= SV_HEIGHT) {
        m_dragTarget = DragTarget::SaturationValue;
        updateFromPointer(event.position);
        return true;
    }
    const float hueX = SV_WIDTH + GAP;
    if (local.x >= hueX && local.x <= hueX + HUE_WIDTH && local.y >= 0.f && local.y <= SV_HEIGHT) {
        m_dragTarget = DragTarget::Hue;
        updateFromPointer(event.position);
        return true;
    }
    m_dragTarget = DragTarget::None;
    return false;
}

bool ColorPicker::pointerMove(PointerEvent &event) {
    if (m_dragTarget == DragTarget::None) { return false; }
    updateFromPointer(event.position);
    return true;
}

bool ColorPicker::pointerUp(PointerEvent &event) {
    (void)event;
    const bool wasDragging = m_dragTarget != DragTarget::None;
    m_dragTarget = DragTarget::None;
    return wasDragging;
}

void ColorPicker::draw(float offsetX, float offsetY) {
    Context *context = getContext();
    if (!context) { return; }
    UIRenderer &renderer = context->getRenderer();

    const Rect frame = getFrame();
    const float baseX = offsetX + frame.origin.x;
    const float baseY = offsetY + frame.origin.y;

    const Color white(0xFFFFFFFF);
    const Color black(0x000000FF);
    const Color hueColor = hsvToRgb(m_hue, 1.f, 1.f);

    // SV palette: a bilinear quad, white→hue along X, →black along Y.
    const Rect svRect(baseX, baseY, SV_WIDTH, SV_HEIGHT);
    renderer.drawGradientRect(svRect, white, hueColor, black, black);

    // Hue strip: 6 vertical gradient segments.
    const float hueX = baseX + SV_WIDTH + GAP;
    const float segment = SV_HEIGHT / 6.f;
    for (int i = 0; i < 6; ++i) {
        const Color top = hsvToRgb(static_cast<float>(i) / 6.f, 1.f, 1.f);
        const Color bottom = hsvToRgb(static_cast<float>(i + 1) / 6.f, 1.f, 1.f);
        const Rect segmentRect(hueX, baseY + static_cast<float>(i) * segment, HUE_WIDTH, segment);
        renderer.drawGradientRect(segmentRect, top, top, bottom, bottom);
    }

    // SV cursor.
    UIRenderer::RectData svCursor;
    svCursor.center = {svRect.origin.x + m_saturation * SV_WIDTH, svRect.origin.y + (1.f - m_value) * SV_HEIGHT, 0.f};
    svCursor.size = {12.f, 12.f};
    svCursor.color = white;
    renderer.drawRoundedRectBorder(svCursor, 6.f, 2.f);

    // Hue cursor.
    UIRenderer::RectData hueCursor;
    hueCursor.center = {hueX + HUE_WIDTH * 0.5f, baseY + m_hue * SV_HEIGHT, 0.f};
    hueCursor.size = {HUE_WIDTH + 6.f, 6.f};
    hueCursor.color = white;
    renderer.drawRoundedRectBorder(hueCursor, 3.f, 2.f);
}

void ColorPicker::themeChanged() {
    m_swatch->surface().setBorderColor(theme().border);
    setNeedsDisplay();
}

} // namespace PandaUI
