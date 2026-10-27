#include "UI/Demo/SkinAssets.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Demo {

namespace {

    struct Rgba {
        float r, g, b, a;
    };

    Rgba rgba(uint32_t hex) {
        return {
            static_cast<float>((hex >> 24) & 0xFF) / 255.f, static_cast<float>((hex >> 16) & 0xFF) / 255.f,
            static_cast<float>((hex >> 8) & 0xFF) / 255.f, static_cast<float>(hex & 0xFF) / 255.f
        };
    }

    Rgba mix(Rgba a, Rgba b, float t) {
        return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
    }

    // A tiny software canvas: rounded rectangles with a vertical gradient and a border, blended over.
    struct Canvas {
        int width, height;
        std::vector<float> pixels; // RGBA, straight alpha

        Canvas(int w, int h)
            : width(w)
            , height(h)
            , pixels(static_cast<size_t>(w) * h * 4, 0.f) {}

        void blend(int x, int y, Rgba color, float coverage) {
            if (x < 0 || y < 0 || x >= width || y >= height) { return; }
            float *p = &pixels[(static_cast<size_t>(y) * width + x) * 4];
            const float a = color.a * coverage;
            p[0] = p[0] * (1.f - a) + color.r * a;
            p[1] = p[1] * (1.f - a) + color.g * a;
            p[2] = p[2] * (1.f - a) + color.b * a;
            p[3] = p[3] * (1.f - a) + a;
        }

        static float roundBox(float px, float py, float hw, float hh, float r) {
            const float qx = std::abs(px) - hw + r;
            const float qy = std::abs(py) - hh + r;
            const float outside = std::sqrt(std::max(qx, 0.f) * std::max(qx, 0.f) + std::max(qy, 0.f) * std::max(qy, 0.f));
            return std::min(std::max(qx, qy), 0.f) + outside - r;
        }

        // A rounded rectangle (inset from the canvas edge) filled with a vertical gradient that is
        // symmetric about the middle (the texture reads the same whichever way the backend stores rows).
        void plate(float inset, float radius, Rgba edge, Rgba middle, Rgba border, float borderWidth) {
            const float hw = width * 0.5f - inset;
            const float hh = height * 0.5f - inset;
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const float px = x + 0.5f - width * 0.5f;
                    const float py = y + 0.5f - height * 0.5f;
                    const float d = roundBox(px, py, hw, hh, radius);
                    const float coverage = std::clamp(0.5f - d, 0.f, 1.f);
                    if (coverage <= 0.f) { continue; }
                    const float t = 1.f - std::abs(py) / hh; // 0 at the edges, 1 in the middle
                    Rgba color = mix(edge, middle, std::clamp(t * 1.4f, 0.f, 1.f));
                    if (borderWidth > 0.f) {
                        const float innerCoverage = std::clamp(0.5f - (d + borderWidth), 0.f, 1.f);
                        color = mix(border, color, innerCoverage);
                    }
                    blend(x, y, color, coverage);
                }
            }
        }

        // A thin rounded line inside the plate (the gilded inner line of a frame).
        void line(float inset, float radius, float thickness, Rgba color) {
            const float hw = width * 0.5f - inset;
            const float hh = height * 0.5f - inset;
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const float px = x + 0.5f - width * 0.5f;
                    const float py = y + 0.5f - height * 0.5f;
                    const float d = std::abs(roundBox(px, py, hw, hh, radius)) - thickness * 0.5f;
                    const float coverage = std::clamp(0.5f - d, 0.f, 1.f);
                    if (coverage > 0.f) { blend(x, y, color, coverage); }
                }
            }
        }

        // A horizontal gradient over the whole canvas.
        void horizontalGradient(Rgba left, Rgba middle, Rgba right) {
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const float t = (x + 0.5f) / width;
                    const Rgba color = t < 0.5f ? mix(left, middle, t * 2.f) : mix(middle, right, (t - 0.5f) * 2.f);
                    blend(x, y, color, 1.f);
                }
            }
        }

        std::vector<unsigned char> bytes() const {
            std::vector<unsigned char> result(pixels.size());
            for (size_t i = 0; i < pixels.size(); ++i) {
                result[i] = static_cast<unsigned char>(std::clamp(pixels[i], 0.f, 1.f) * 255.f + 0.5f);
            }
            return result;
        }
    };

    PandaUI::TextureHandle upload(const CreateTextureFn &createTexture, const Canvas &canvas) {
        const std::vector<unsigned char> bytes = canvas.bytes();
        return createTexture(bytes.data(), static_cast<uint16_t>(canvas.width), static_cast<uint16_t>(canvas.height));
    }

    // The palette of the skin: aged parchment, dark wood, gilding.
    const Rgba kWoodDark = rgba(0x2A1A0EFF);
    const Rgba kWood = rgba(0x4A2E18FF);
    const Rgba kWoodLight = rgba(0x6B4426FF);
    const Rgba kGold = rgba(0xC9A227FF);
    const Rgba kGoldLight = rgba(0xF0D27AFF);
    const Rgba kGoldDark = rgba(0x8A6A14FF);
    const Rgba kParchment = rgba(0x3A2816FF);

} // namespace

SkinAssets createSkinAssets(const CreateTextureFn &createTexture) {
    SkinAssets assets;
    {
        // The card frame: a dark wooden rim with a gilded inner line over a parchment fill.
        Canvas canvas(96, 96);
        canvas.plate(1.f, 18.f, kWoodDark, kWood, kWoodDark, 3.f);
        canvas.plate(7.f, 13.f, kParchment, kParchment, kGold, 1.5f);
        canvas.line(10.f, 10.f, 1.f, mix(kGold, kParchment, 0.6f));
        assets.frame = upload(createTexture, canvas);
    }
    const auto plate = [&](Rgba edge, Rgba middle, Rgba border) {
        Canvas canvas(64, 64);
        canvas.plate(1.f, 14.f, edge, middle, border, 2.f);
        canvas.line(5.f, 10.f, 1.f, mix(kGoldLight, middle, 0.5f));
        return upload(createTexture, canvas);
    };
    assets.plateNormal = plate(kWood, kWoodLight, kGoldDark);
    assets.plateHovered = plate(kWoodLight, mix(kWoodLight, kGold, 0.25f), kGold);
    assets.platePressed = plate(kWoodDark, kWood, kGoldDark);
    assets.plateDisabled = plate(mix(kWoodDark, kParchment, 0.5f), mix(kWood, kParchment, 0.5f), mix(kGoldDark, kParchment, 0.6f));
    {
        // A recessed groove: dark in the middle, a hairline of gilding at the rim.
        Canvas canvas(48, 48);
        canvas.plate(1.f, 12.f, kWood, kWoodDark, kGoldDark, 1.5f);
        assets.track = upload(createTexture, canvas);
    }
    {
        Canvas canvas(64, 16);
        canvas.horizontalGradient(kGoldDark, kGoldLight, kGold);
        assets.goldFill = upload(createTexture, canvas);
    }
    return assets;
}

} // namespace Demo
