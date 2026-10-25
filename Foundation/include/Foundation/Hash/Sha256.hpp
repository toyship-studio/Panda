#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Foundation {

// A SHA-256 digest: 32 bytes, printed and parsed as 64 lowercase hex digits (the form of the
// release manifests and the plugin registry).
struct Sha256Digest {
    uint8_t bytes[32] = {};

    std::string toHex() const;
    static std::optional<Sha256Digest> fromHex(std::string_view hex);

    bool operator==(const Sha256Digest &other) const;
    bool operator!=(const Sha256Digest &other) const {
        return !(*this == other);
    }
};

// Incremental SHA-256 (FIPS 180-4): update() with the data as it arrives, finish() once. A
// download is hashed while its bytes are written, so a file is never read a second time to
// check it.
class Sha256 {
public:
    Sha256();

    void update(const void *data, size_t size);
    Sha256Digest finish();
    void reset();

    static Sha256Digest of(const void *data, size_t size);

private:
    void processBlock(const uint8_t *block);

    uint32_t m_state[8];
    uint8_t m_buffer[64];
    size_t m_buffered = 0;
    uint64_t m_total = 0;
};

} // namespace Foundation
