#pragma once

// Test-only SHA-256 for reproducible evidence. This is not an authentication
// mechanism; byte-exact admission checks remain independent of the digest.
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace astraea::test {

using Sha256 = std::array<std::byte, 32U>;

[[nodiscard]] inline Sha256 owned_pair_sha256(
    std::span<const std::byte> data) noexcept {
    constexpr std::array<std::uint32_t, 64U> k{
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
        0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
        0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
        0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
        0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
        0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
        0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
        0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
        0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
        0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
        0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
        0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
        0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
        0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
        0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
        0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
    };
    std::array<std::uint32_t, 8U> h{
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
    };

    const auto compress = [&](std::span<const std::byte, 64U> block) {
        std::array<std::uint32_t, 64U> w{};
        for (std::size_t i = 0U; i < 16U; ++i) {
            for (std::size_t j = 0U; j < 4U; ++j) {
                w[i] = (w[i] << 8U) |
                    std::to_integer<std::uint32_t>(block[4U * i + j]);
            }
        }
        for (std::size_t i = 16U; i < 64U; ++i) {
            const auto s0 =
                std::rotr(w[i - 15U], 7) ^
                std::rotr(w[i - 15U], 18) ^
                (w[i - 15U] >> 3U);
            const auto s1 =
                std::rotr(w[i - 2U], 17) ^
                std::rotr(w[i - 2U], 19) ^
                (w[i - 2U] >> 10U);
            w[i] = w[i - 16U] + s0 + w[i - 7U] + s1;
        }
        auto a = h[0], b = h[1], c = h[2], d = h[3];
        auto e = h[4], f = h[5], g = h[6], q = h[7];
        for (std::size_t i = 0U; i < 64U; ++i) {
            const auto s1 =
                std::rotr(e, 6) ^ std::rotr(e, 11) ^
                std::rotr(e, 25);
            const auto t1 =
                q + s1 + ((e & f) ^ (~e & g)) + k[i] + w[i];
            const auto s0 =
                std::rotr(a, 2) ^ std::rotr(a, 13) ^
                std::rotr(a, 22);
            const auto t2 =
                s0 + ((a & b) ^ (a & c) ^ (b & c));
            q = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += q;
    };

    std::size_t at = 0U;
    while (data.size() - at >= 64U) {
        compress(std::span<const std::byte, 64U>{
            data.data() + at, 64U});
        at += 64U;
    }
    std::array<std::byte, 64U> tail{};
    const auto left = data.size() - at;
    for (std::size_t i = 0U; i < left; ++i)
        tail[i] = data[at + i];
    tail[left] = std::byte{0x80};
    if (left >= 56U) {
        compress(tail);
        tail.fill(std::byte{0});
    }
    const auto length_bits =
        static_cast<std::uint64_t>(data.size()) * 8U;
    for (std::size_t i = 0U; i < 8U; ++i) {
        tail[56U + i] = static_cast<std::byte>(
            (length_bits >> (8U * (7U - i))) & 0xffU);
    }
    compress(tail);
    Sha256 digest{};
    for (std::size_t i = 0U; i < 8U; ++i) {
        for (std::size_t j = 0U; j < 4U; ++j) {
            digest[4U * i + j] = static_cast<std::byte>(
                (h[i] >> (8U * (3U - j))) & 0xffU);
        }
    }
    return digest;
}

[[nodiscard]] inline std::string owned_pair_sha256_hex(
    std::span<const std::byte> data) {
    constexpr char digits[] = "0123456789abcdef";
    const auto digest = owned_pair_sha256(data);
    std::string result(64U, '0');
    for (std::size_t i = 0U; i < digest.size(); ++i) {
        const auto value = std::to_integer<unsigned>(digest[i]);
        result[2U * i] = digits[value >> 4U];
        result[2U * i + 1U] = digits[value & 0xfU];
    }
    return result;
}

}  // namespace astraea::test
