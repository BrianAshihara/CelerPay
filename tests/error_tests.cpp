#include <celerpay/error.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace std;
using namespace celerpay;

TEST_CASE("toString cobre todos os codigos", "[error]") {
    STATIC_REQUIRE(toString(ErrorCode::EmptyInput) == "EmptyInput");
    STATIC_REQUIRE(toString(ErrorCode::InvalidCharacter) == "InvalidCharacter");
    STATIC_REQUIRE(toString(ErrorCode::UnexpectedEnd) == "UnexpectedEnd");
    STATIC_REQUIRE(toString(ErrorCode::TooManyDecimals) == "TooManyDecimals");
    STATIC_REQUIRE(toString(ErrorCode::Overflow) == "Overflow");
    STATIC_REQUIRE(toString(ErrorCode::InvalidLength) == "InvalidLength");
    STATIC_REQUIRE(toString(ErrorCode::InvalidCheckDigit) == "InvalidCheckDigit");
    STATIC_REQUIRE(toString(ErrorCode::RepeatedDigits) == "RepeatedDigits");
    STATIC_REQUIRE(toString(ErrorCode::ValueTooLong) == "ValueTooLong");
    STATIC_REQUIRE(toString(ErrorCode::MissingField) == "MissingField");
    STATIC_REQUIRE(toString(ErrorCode::InvalidValue) == "InvalidValue");
    STATIC_REQUIRE(toString(ErrorCode::TooManyFields) == "TooManyFields");
    STATIC_REQUIRE(toString(ErrorCode::IoError) == "IoError");
}

TEST_CASE("Error compara codigo, offset e detalhe", "[error]") {
    const Error a{ErrorCode::Overflow, 3, "x"};
    CHECK(a == Error{ErrorCode::Overflow, 3, "x"});
    CHECK(a != Error{ErrorCode::Overflow, 4, "x"});
    CHECK(a != Error{ErrorCode::EmptyInput, 3, "x"});
}

TEST_CASE("Result carrega valor ou erro", "[error]") {
    const Result<int> ok = 42;
    const Result<int> bad = unexpected(Error{ErrorCode::EmptyInput, 0, {}});
    REQUIRE(ok.has_value());
    CHECK(*ok == 42);
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().code == ErrorCode::EmptyInput);
}
