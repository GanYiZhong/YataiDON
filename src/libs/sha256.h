#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>

namespace crypto {

class Sha256 {
public:
    void update(const uint8_t* data, size_t len) {
        total_len += len;
        while (len > 0) {
            size_t n = std::min(len, size_t(64) - buffer_len);
            std::memcpy(buffer + buffer_len, data, n);
            buffer_len += n;
            data += n;
            len -= n;
            if (buffer_len == 64) {
                process(buffer);
                buffer_len = 0;
            }
        }
    }

    std::array<uint8_t, 32> finalize() {
        uint64_t bit_len = total_len * 8;
        uint8_t pad = 0x80;
        update(&pad, 1);
        uint8_t zero = 0;
        while (buffer_len != 56) update(&zero, 1);
        for (int i = 0; i < 8; ++i) buffer[56 + i] = static_cast<uint8_t>(bit_len >> (56 - 8 * i));
        process(buffer);

        std::array<uint8_t, 32> out;
        for (int i = 0; i < 8; ++i) {
            out[i * 4 + 0] = static_cast<uint8_t>(h[i] >> 24);
            out[i * 4 + 1] = static_cast<uint8_t>(h[i] >> 16);
            out[i * 4 + 2] = static_cast<uint8_t>(h[i] >> 8);
            out[i * 4 + 3] = static_cast<uint8_t>(h[i]);
        }
        return out;
    }

    static std::array<uint8_t, 32> hash(const uint8_t* data, size_t len) {
        Sha256 ctx;
        ctx.update(data, len);
        return ctx.finalize();
    }

    static std::array<uint8_t, 32> hash(const std::string& data) {
        return hash(reinterpret_cast<const uint8_t*>(data.data()), data.size());
    }

private:
    uint32_t h[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };
    uint8_t buffer[64];
    size_t buffer_len = 0;
    uint64_t total_len = 0;

    static uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

    void process(const uint8_t block[64]) {
        static constexpr uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
        };

        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (uint32_t(block[i * 4]) << 24) | (uint32_t(block[i * 4 + 1]) << 16) |
                   (uint32_t(block[i * 4 + 2]) << 8) | uint32_t(block[i * 4 + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t temp1 = hh + S1 + ch + k[i] + w[i];
            uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t temp2 = S0 + maj;
            hh = g; g = f; f = e; e = d + temp1;
            d = c; c = b; b = a; a = temp1 + temp2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
};

inline std::array<uint8_t, 32> sha256(const std::string& data) {
    return Sha256::hash(data);
}

// HMAC-SHA256 per RFC 2104.
inline std::array<uint8_t, 32> hmac_sha256(const std::string& key, const std::string& message) {
    constexpr size_t block_size = 64;
    std::array<uint8_t, block_size> key_block{};
    if (key.size() > block_size) {
        auto hashed = sha256(key);
        std::copy(hashed.begin(), hashed.end(), key_block.begin());
    } else {
        std::copy(key.begin(), key.end(), key_block.begin());
    }

    std::array<uint8_t, block_size> ipad, opad;
    for (size_t i = 0; i < block_size; ++i) {
        ipad[i] = key_block[i] ^ 0x36;
        opad[i] = key_block[i] ^ 0x5c;
    }

    Sha256 inner;
    inner.update(ipad.data(), block_size);
    inner.update(reinterpret_cast<const uint8_t*>(message.data()), message.size());
    auto inner_hash = inner.finalize();

    Sha256 outer;
    outer.update(opad.data(), block_size);
    outer.update(inner_hash.data(), inner_hash.size());
    return outer.finalize();
}

inline std::string to_hex(const std::array<uint8_t, 32>& bytes) {
    static constexpr char hex_chars[] = "0123456789abcdef";
    std::string out(64, '0');
    for (size_t i = 0; i < bytes.size(); ++i) {
        out[i * 2] = hex_chars[bytes[i] >> 4];
        out[i * 2 + 1] = hex_chars[bytes[i] & 0xF];
    }
    return out;
}

}  // namespace crypto
