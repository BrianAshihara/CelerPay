#include <celerpay/money.hpp>

#include <catch2/catch_test_macros.hpp>

#include <limits>

using namespace std;
using namespace celerpay;

namespace {

constexpr int64_t kMax = numeric_limits<int64_t>::max();
constexpr int64_t kMin = numeric_limits<int64_t>::min();

Money cents(int64_t value) { return Money::fromCents(value); }

void requireError(string_view text, ErrorCode code, uint64_t offset) {
    const auto result = parseAmount(text);
    INFO("entrada: \"" << text << "\"");
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == code);
    CHECK(result.error().offset == offset);
    CHECK_FALSE(result.error().detail.empty());
}

}

TEST_CASE("Money basico", "[money]") {
    STATIC_REQUIRE(Money{}.cents() == 0);
    STATIC_REQUIRE(Money::fromCents(-150).cents() == -150);
    STATIC_REQUIRE(Money::fromCents(1) < Money::fromCents(2));
    STATIC_REQUIRE(Money::fromCents(5) == Money::fromCents(5));
}

TEST_CASE("Money aritmetica", "[money]") {
    CHECK((cents(150) + cents(250)).cents() == 400);
    CHECK((cents(150) - cents(250)).cents() == -100);
    CHECK((cents(150) * 3).cents() == 450);
    CHECK((3 * cents(150)).cents() == 450);
    CHECK((-cents(150)).cents() == -150);

    Money m = cents(100);
    m += cents(50);
    m -= cents(20);
    m *= 2;
    CHECK(m.cents() == 260);
}

TEST_CASE("checked detecta overflow", "[money]") {
    CHECK(checkedAdd(cents(kMax - 1), cents(1))->cents() == kMax);
    CHECK(checkedAdd(cents(kMax), cents(1)).error().code == ErrorCode::Overflow);
    CHECK(checkedAdd(cents(kMin), cents(-1)).error().code == ErrorCode::Overflow);

    CHECK(checkedSub(cents(kMin + 1), cents(1))->cents() == kMin);
    CHECK(checkedSub(cents(kMin), cents(1)).error().code == ErrorCode::Overflow);
    CHECK(checkedSub(cents(kMax), cents(-1)).error().code == ErrorCode::Overflow);
    CHECK(checkedSub(cents(0), cents(kMin)).error().code == ErrorCode::Overflow);

    CHECK(checkedMul(cents(kMax / 2), 2)->cents() == kMax - 1);
    CHECK(checkedMul(cents(kMax / 2 + 1), 2).error().code == ErrorCode::Overflow);
    CHECK(checkedMul(cents(kMin), -1).error().code == ErrorCode::Overflow);
    CHECK(checkedMul(cents(-1), kMin).error().code == ErrorCode::Overflow);
    CHECK(checkedMul(cents(kMin / 2), 2)->cents() == kMin);
    CHECK(checkedMul(cents(-3), -4)->cents() == 12);
    CHECK(checkedMul(cents(kMin), 0)->cents() == 0);

    CHECK(checkedNeg(cents(kMax))->cents() == -kMax);
    CHECK(checkedNeg(cents(kMin)).error().code == ErrorCode::Overflow);
}

TEST_CASE("parseAmount aceita o formato PIX", "[money][parse]") {
    CHECK(parseAmount("0")->cents() == 0);
    CHECK(parseAmount("10")->cents() == 1000);
    CHECK(parseAmount("10.5")->cents() == 1050);
    CHECK(parseAmount("10.50")->cents() == 1050);
    CHECK(parseAmount("10.05")->cents() == 1005);
    CHECK(parseAmount("0.01")->cents() == 1);
    CHECK(parseAmount("007.50")->cents() == 750);
    CHECK(parseAmount("1234.56")->cents() == 123456);
    CHECK(parseAmount("92233720368547758.07")->cents() == kMax);
}

TEST_CASE("parseAmount recusa formatos invalidos", "[money][parse]") {
    requireError("", ErrorCode::EmptyInput, 0);
    requireError("-1.00", ErrorCode::InvalidCharacter, 0);
    requireError("+1.00", ErrorCode::InvalidCharacter, 0);
    requireError(" 1.00", ErrorCode::InvalidCharacter, 0);
    requireError("1.00 ", ErrorCode::InvalidCharacter, 4);
    requireError(".5", ErrorCode::InvalidCharacter, 0);
    requireError("1,50", ErrorCode::InvalidCharacter, 1);
    requireError("1.234.56", ErrorCode::TooManyDecimals, 4);
    requireError("1.2a", ErrorCode::InvalidCharacter, 3);
    requireError("10.", ErrorCode::UnexpectedEnd, 3);
    requireError("10.505", ErrorCode::TooManyDecimals, 5);
    requireError("92233720368547758.08", ErrorCode::Overflow, 19);
    requireError("92233720368547759", ErrorCode::Overflow, 16);
    requireError("999999999999999999999", ErrorCode::Overflow, 16);
}

TEST_CASE("formatBrl", "[money][format]") {
    CHECK(formatBrl(cents(0)) == "R$ 0,00");
    CHECK(formatBrl(cents(5)) == "R$ 0,05");
    CHECK(formatBrl(cents(100)) == "R$ 1,00");
    CHECK(formatBrl(cents(99999)) == "R$ 999,99");
    CHECK(formatBrl(cents(123456)) == "R$ 1.234,56");
    CHECK(formatBrl(cents(100000000)) == "R$ 1.000.000,00");
    CHECK(formatBrl(cents(-123456)) == "-R$ 1.234,56");
    CHECK(formatBrl(cents(-1)) == "-R$ 0,01");
    CHECK(formatBrl(cents(kMax)) == "R$ 92.233.720.368.547.758,07");
    CHECK(formatBrl(cents(kMin)) == "-R$ 92.233.720.368.547.758,08");
}

TEST_CASE("parseAmount le a forma canonica com duas casas", "[money]") {
    for (const int64_t value : {int64_t{0}, int64_t{1}, int64_t{99}, int64_t{100}, int64_t{123456}, kMax}) {
        const int64_t fraction = value % 100;
        const string text = to_string(value / 100) + (fraction < 10 ? ".0" : ".") + to_string(fraction);
        const auto parsed = parseAmount(text);
        REQUIRE(parsed.has_value());
        CHECK(parsed->cents() == value);
    }
}
