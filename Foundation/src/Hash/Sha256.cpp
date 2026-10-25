#include "Foundation/Hash/Sha256.hpp"

#include <algorithm>
#include <cstring>

namespace Foundation {

namespace {

constexpr uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

inline uint32_t rotateRight(uint32_t value, int bits) {
    return (value >> bits) | (value << (32 - bits));
}

inline uint32_t readBigEndian(const uint8_t *bytes) {
    return (uint32_t(bytes[0]) << 24) | (uint32_t(bytes[1]) << 16) | (uint32_t(bytes[2]) << 8) | uint32_t(bytes[3]);
}

inline void writeBigEndian(uint8_t *bytes, uint32_t value) {
    bytes[0] = uint8_t(value >> 24);
    bytes[1] = uint8_t(value >> 16);
    bytes[2] = uint8_t(value >> 8);
    bytes[3] = uint8_t(value);
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
    return -1;
}

} // namespace

std::string Sha256Digest::toHex() const {
    static const char digits[] = "0123456789abcdef";
    std::string hex(64, '0');
    for (size_t i = 0; i < 32; ++i) {
        hex[i * 2] = digits[bytes[i] >> 4];
        hex[i * 2 + 1] = digits[bytes[i] & 0xf];
    }
    return hex;
}

std::optional<Sha256Digest> Sha256Digest::fromHex(std::string_view hex) {
    if (hex.size() != 64) { return std::nullopt; }
    Sha256Digest digest;
    for (size_t i = 0; i < 32; ++i) {
        int high = hexValue(hex[i * 2]);
        int low = hexValue(hex[i * 2 + 1]);
        if (high < 0 || low < 0) { return std::nullopt; }
        digest.bytes[i] = uint8_t((high << 4) | low);
    }
    return digest;
}

bool Sha256Digest::operator==(const Sha256Digest &other) const {
    return std::memcmp(bytes, other.bytes, sizeof(bytes)) == 0;
}

Sha256::Sha256() {
    reset();
}

void Sha256::reset() {
    static constexpr uint32_t initial[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
    };
    std::memcpy(m_state, initial, sizeof(m_state));
    m_buffered = 0;
    m_total = 0;
}

void Sha256::update(const void *data, size_t size) {
    const uint8_t *bytes = static_cast<const uint8_t *>(data);
    m_total += size;
    if (m_buffered > 0) {
        size_t take = std::min(size, sizeof(m_buffer) - m_buffered);
        std::memcpy(m_buffer + m_buffered, bytes, take);
        m_buffered += take;
        bytes += take;
        size -= take;
        if (m_buffered < sizeof(m_buffer)) { return; }
        processBlock(m_buffer);
        m_buffered = 0;
    }
    while (size >= sizeof(m_buffer)) {
        processBlock(bytes);
        bytes += sizeof(m_buffer);
        size -= sizeof(m_buffer);
    }
    if (size > 0) {
        std::memcpy(m_buffer, bytes, size);
        m_buffered = size;
    }
}

Sha256Digest Sha256::finish() {
    uint64_t bitLength = m_total * 8;
    uint8_t terminator = 0x80;
    update(&terminator, 1);
    m_total -= 1; // the padding is not part of the message
    uint8_t zero = 0;
    while (m_buffered != 56) {
        update(&zero, 1);
        m_total -= 1;
    }
    uint8_t length[8];
    for (int i = 7; i >= 0; --i) {
        length[i] = uint8_t(bitLength & 0xff);
        bitLength >>= 8;
    }
    update(length, 8);

    Sha256Digest digest;
    for (size_t i = 0; i < 8; ++i) { writeBigEndian(digest.bytes + i * 4, m_state[i]); }
    reset();
    return digest;
}

Sha256Digest Sha256::of(const void *data, size_t size) {
    Sha256 hasher;
    hasher.update(data, size);
    return hasher.finish();
}

void Sha256::processBlock(const uint8_t *block) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) { w[i] = readBigEndian(block + i * 4); }
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = rotateRight(w[i - 15], 7) ^ rotateRight(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotateRight(w[i - 2], 17) ^ rotateRight(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = m_state[0], b = m_state[1], c = m_state[2], d = m_state[3];
    uint32_t e = m_state[4], f = m_state[5], g = m_state[6], h = m_state[7];
    for (int i = 0; i < 64; ++i) {
        uint32_t S1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t temp1 = h + S1 + ch + K[i] + w[i];
        uint32_t S0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }
    m_state[0] += a;
    m_state[1] += b;
    m_state[2] += c;
    m_state[3] += d;
    m_state[4] += e;
    m_state[5] += f;
    m_state[6] += g;
    m_state[7] += h;
}

} // namespace Foundation
