#include "Project/Import/TextureCompressor.hpp"

#include <bc7enc.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace Panda::TextureCompressor {

namespace {

    struct Level {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint8_t> pixels; // RGBA8
    };

    // sRGB ↔ linear by tables: a level of a color texture is the average of the light, not of
    // the encoded values (averaged as numbers, a texture darkens with the distance).
    struct ColorTables {
        float toLinear[256];
        uint8_t toSrgb[4096];
        ColorTables() {
            for (int i = 0; i < 256; ++i) {
                const float c = static_cast<float>(i) / 255.f;
                toLinear[i] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
            }
            for (int i = 0; i < 4096; ++i) {
                const float c = static_cast<float>(i) / 4095.f;
                const float s = c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.f / 2.4f) - 0.055f;
                toSrgb[i] = static_cast<uint8_t>(std::clamp(s * 255.f + 0.5f, 0.f, 255.f));
            }
        }
    };

    // The next level: every texel is the average of the 2×2 texels over it (an odd side keeps
    // its last row or column by reading it twice).
    Level downsample(const Level &source, bool srgb) {
        static const ColorTables TABLES;
        Level level;
        level.width = std::max(source.width / 2, 1u);
        level.height = std::max(source.height / 2, 1u);
        level.pixels.resize(static_cast<size_t>(level.width) * level.height * 4);
        for (uint32_t y = 0; y < level.height; ++y) {
            const uint32_t y0 = std::min(y * 2, source.height - 1);
            const uint32_t y1 = std::min(y * 2 + 1, source.height - 1);
            for (uint32_t x = 0; x < level.width; ++x) {
                const uint32_t x0 = std::min(x * 2, source.width - 1);
                const uint32_t x1 = std::min(x * 2 + 1, source.width - 1);
                const uint8_t *taps[4] = {
                    &source.pixels[(static_cast<size_t>(y0) * source.width + x0) * 4],
                    &source.pixels[(static_cast<size_t>(y0) * source.width + x1) * 4],
                    &source.pixels[(static_cast<size_t>(y1) * source.width + x0) * 4],
                    &source.pixels[(static_cast<size_t>(y1) * source.width + x1) * 4],
                };
                uint8_t *out = &level.pixels[(static_cast<size_t>(y) * level.width + x) * 4];
                for (int channel = 0; channel < 4; ++channel) {
                    if (srgb && channel < 3) {
                        const float sum = TABLES.toLinear[taps[0][channel]] + TABLES.toLinear[taps[1][channel]] +
                                          TABLES.toLinear[taps[2][channel]] + TABLES.toLinear[taps[3][channel]];
                        out[channel] = TABLES.toSrgb[static_cast<int>(sum * 0.25f * 4095.f + 0.5f)];
                    } else {
                        const int sum = taps[0][channel] + taps[1][channel] + taps[2][channel] + taps[3][channel];
                        out[channel] = static_cast<uint8_t>((sum + 2) / 4);
                    }
                }
            }
        }
        return level;
    }

    // A level → its blocks, rows of blocks shared out among the cores.
    void encode(const Level &level, uint8_t *blocks, const bc7enc_compress_block_params &params) {
        const uint32_t blocksWide = (level.width + 3) / 4;
        const uint32_t blocksHigh = (level.height + 3) / 4;
        std::atomic<uint32_t> nextRow{0};
        auto work = [&] {
            for (uint32_t by = nextRow.fetch_add(1); by < blocksHigh; by = nextRow.fetch_add(1)) {
                for (uint32_t bx = 0; bx < blocksWide; ++bx) {
                    // The 4×4 texels of the block; past the edge of the image the edge repeats.
                    uint8_t texels[16 * 4];
                    for (uint32_t y = 0; y < 4; ++y) {
                        const uint32_t sy = std::min(by * 4 + y, level.height - 1);
                        for (uint32_t x = 0; x < 4; ++x) {
                            const uint32_t sx = std::min(bx * 4 + x, level.width - 1);
                            std::memcpy(&texels[(y * 4 + x) * 4], &level.pixels[(static_cast<size_t>(sy) * level.width + sx) * 4], 4);
                        }
                    }
                    bc7enc_compress_block(blocks + (static_cast<size_t>(by) * blocksWide + bx) * 16, texels, &params);
                }
            }
        };
        const uint32_t threads = std::min(std::max(std::thread::hardware_concurrency(), 1u), blocksHigh);
        if (threads <= 1) {
            work();
            return;
        }
        std::vector<std::thread> pool;
        pool.reserve(threads - 1);
        for (uint32_t i = 1; i < threads; ++i) { pool.emplace_back(work); }
        work();
        for (std::thread &thread : pool) { thread.join(); }
    }

} // namespace

Miren::TextureCreate compress(const Miren::TextureCreate &source, bool srgb) {
    Miren::TextureCreate result;
    if (source.m_format != Miren::RGBA8 || source.m_isCubeMap || source.is3D() || source.m_data.data == nullptr ||
        source.m_width == 0 || source.m_height == 0) {
        return result;
    }
    static std::once_flag initialized;
    std::call_once(initialized, [] { bc7enc_compress_block_init(); });
    bc7enc_compress_block_params params;
    bc7enc_compress_block_params_init(&params);
    // Mode 6 and a few partitions of mode 1: most of the quality of the format for a fraction
    // of the time an exhaustive search takes — a project of 4K scans is cooked in seconds.
    params.m_max_partitions_mode = 16;
    params.m_uber_level = 0;
    if (!srgb) { bc7enc_compress_block_params_init_linear_weights(&params); }

    std::vector<Level> levels(1);
    levels[0].width = source.m_width;
    levels[0].height = source.m_height;
    levels[0].pixels.assign(
        static_cast<const uint8_t *>(source.m_data.data),
        static_cast<const uint8_t *>(source.m_data.data) + static_cast<size_t>(source.m_width) * source.m_height * 4
    );
    while (levels.back().width > 1 || levels.back().height > 1) { levels.push_back(downsample(levels.back(), srgb)); }

    result = source;
    result.m_format = Miren::BC7;
    result.m_dataMips = static_cast<uint8_t>(levels.size() - 1);
    const size_t size = result.dataBytes();
    result.m_data = Foundation::Memory::alloc(static_cast<uint32_t>(size));
    uint8_t *blocks = static_cast<uint8_t *>(result.m_data.data);
    for (uint32_t index = 0; index < levels.size(); ++index) {
        encode(levels[index], blocks, params);
        blocks += result.levelBytes(index);
    }
    return result;
}

} // namespace Panda::TextureCompressor
