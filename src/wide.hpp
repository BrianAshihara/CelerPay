#pragma once

#include <cstdint>
#include <limits>
#include <optional>

namespace celerpay::detail {

struct U128 {
    std::uint64_t hi = 0;
    std::uint64_t lo = 0;
};

constexpr U128 mul64(std::uint64_t a, std::uint64_t b) noexcept {
    const std::uint64_t a0 = a & 0xFFFFFFFF;
    const std::uint64_t a1 = a >> 32;
    const std::uint64_t b0 = b & 0xFFFFFFFF;
    const std::uint64_t b1 = b >> 32;
    const std::uint64_t p00 = a0 * b0;
    const std::uint64_t p01 = a0 * b1;
    const std::uint64_t p10 = a1 * b0;
    const std::uint64_t p11 = a1 * b1;
    const std::uint64_t mid = (p00 >> 32) + (p01 & 0xFFFFFFFF) + (p10 & 0xFFFFFFFF);
    return {p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32), (p00 & 0xFFFFFFFF) | (mid << 32)};
}

constexpr std::optional<U128> mul(U128 x, std::uint64_t m) noexcept {
    const U128 low = mul64(x.lo, m);
    const U128 high = mul64(x.hi, m);
    if (high.hi != 0 || high.lo > std::numeric_limits<std::uint64_t>::max() - low.hi) {
        return std::nullopt;
    }
    return U128{high.lo + low.hi, low.lo};
}

struct DivResult {
    U128 quotient;
    std::uint64_t remainder = 0;
};

constexpr DivResult divmod(U128 x, std::uint64_t d) noexcept {
    DivResult out;
    std::uint64_t remainder = 0;
    for (int bit = 127; bit >= 0; --bit) {
        const bool carry = (remainder >> 63) != 0;
        remainder = (remainder << 1) | ((bit >= 64 ? x.hi >> (bit - 64) : x.lo >> bit) & 1);
        if (carry || remainder >= d) {
            remainder -= d;
            if (bit >= 64) {
                out.quotient.hi |= std::uint64_t{1} << (bit - 64);
            } else {
                out.quotient.lo |= std::uint64_t{1} << bit;
            }
        }
    }
    out.remainder = remainder;
    return out;
}

/// round_half_up(a * b * c / d) para valores não negativos. Vazio se o resultado não couber em uint64.
constexpr std::optional<std::uint64_t> mulDivRound(std::uint64_t a, std::uint64_t b, std::uint64_t c, std::uint64_t d) noexcept {
    const auto product = mul(mul64(a, b), c);
    if (!product) {
        return std::nullopt;
    }
    const DivResult result = divmod(*product, d);
    if (result.quotient.hi != 0) {
        return std::nullopt;
    }
    std::uint64_t quotient = result.quotient.lo;
    if (result.remainder >= d - result.remainder) {
        if (quotient == std::numeric_limits<std::uint64_t>::max()) {
            return std::nullopt;
        }
        ++quotient;
    }
    return quotient;
}

}
