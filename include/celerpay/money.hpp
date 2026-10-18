#pragma once

#include <celerpay/error.hpp>
#include <celerpay/export.hpp>

#include <cassert>
#include <compare>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace celerpay {

class Money {
public:
    constexpr Money() noexcept = default;

    static constexpr Money fromCents(std::int64_t cents) noexcept { return Money{cents}; }

    constexpr std::int64_t cents() const noexcept { return cents_; }

    friend constexpr auto operator<=>(Money, Money) noexcept = default;

    constexpr Money operator-() const noexcept;
    constexpr Money& operator+=(Money other) noexcept;
    constexpr Money& operator-=(Money other) noexcept;
    constexpr Money& operator*=(std::int64_t factor) noexcept;

private:
    explicit constexpr Money(std::int64_t cents) noexcept : cents_{cents} {}

    std::int64_t cents_ = 0;
};

namespace detail {

inline constexpr std::int64_t kMaxCents = std::numeric_limits<std::int64_t>::max();
inline constexpr std::int64_t kMinCents = std::numeric_limits<std::int64_t>::min();

constexpr bool addOverflows(std::int64_t a, std::int64_t b) noexcept {
    return (b > 0 && a > kMaxCents - b) || (b < 0 && a < kMinCents - b);
}

constexpr bool subOverflows(std::int64_t a, std::int64_t b) noexcept {
    return (b < 0 && a > kMaxCents + b) || (b > 0 && a < kMinCents + b);
}

constexpr bool mulOverflows(std::int64_t a, std::int64_t b) noexcept {
    if (a == 0 || b == 0) {
        return false;
    }
    if (a > 0) {
        return b > 0 ? a > kMaxCents / b : b < kMinCents / a;
    }
    return b > 0 ? a < kMinCents / b : a < kMaxCents / b;
}

inline constexpr Error kOverflow{ErrorCode::Overflow, 0, "resultado excede o limite de int64 em centavos"};

}

constexpr Money operator+(Money a, Money b) noexcept {
    assert(!detail::addOverflows(a.cents(), b.cents()));
    return Money::fromCents(a.cents() + b.cents());
}

constexpr Money operator-(Money a, Money b) noexcept {
    assert(!detail::subOverflows(a.cents(), b.cents()));
    return Money::fromCents(a.cents() - b.cents());
}

constexpr Money operator*(Money a, std::int64_t factor) noexcept {
    assert(!detail::mulOverflows(a.cents(), factor));
    return Money::fromCents(a.cents() * factor);
}

constexpr Money operator*(std::int64_t factor, Money a) noexcept { return a * factor; }

constexpr Money Money::operator-() const noexcept {
    assert(cents_ != detail::kMinCents);
    return Money{-cents_};
}

constexpr Money& Money::operator+=(Money other) noexcept { return *this = *this + other; }

constexpr Money& Money::operator-=(Money other) noexcept { return *this = *this - other; }

constexpr Money& Money::operator*=(std::int64_t factor) noexcept { return *this = *this * factor; }

constexpr Result<Money> checkedAdd(Money a, Money b) noexcept {
    if (detail::addOverflows(a.cents(), b.cents())) {
        return std::unexpected(detail::kOverflow);
    }
    return Money::fromCents(a.cents() + b.cents());
}

constexpr Result<Money> checkedSub(Money a, Money b) noexcept {
    if (detail::subOverflows(a.cents(), b.cents())) {
        return std::unexpected(detail::kOverflow);
    }
    return Money::fromCents(a.cents() - b.cents());
}

constexpr Result<Money> checkedMul(Money a, std::int64_t factor) noexcept {
    if (detail::mulOverflows(a.cents(), factor)) {
        return std::unexpected(detail::kOverflow);
    }
    return Money::fromCents(a.cents() * factor);
}

constexpr Result<Money> checkedNeg(Money a) noexcept {
    if (a.cents() == detail::kMinCents) {
        return std::unexpected(detail::kOverflow);
    }
    return Money::fromCents(-a.cents());
}

/// Lê um valor no formato do campo 54 do PIX: "10", "10.5" ou "10.50".
CELERPAY_API Result<Money> parseAmount(std::string_view text) noexcept;

/// Formata como "R$ 1.234,56" ou "-R$ 1.234,56".
CELERPAY_API std::string formatBrl(Money value);

}
