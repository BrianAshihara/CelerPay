#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string_view>

namespace celerpay {

enum class ErrorCode : std::uint16_t {
    EmptyInput,
    InvalidCharacter,
    UnexpectedEnd,
    TooManyDecimals,
    Overflow,
    InvalidLength,
    InvalidCheckDigit,
    RepeatedDigits,
    ValueTooLong,
    MissingField,
    InvalidValue,
    TooManyFields,
    IoError,
};

struct Error {
    ErrorCode code;
    std::uint64_t offset = 0;
    std::string_view detail;

    friend constexpr bool operator==(const Error&, const Error&) noexcept = default;
};

template <class T>
using Result = std::expected<T, Error>;

constexpr std::string_view toString(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::EmptyInput:
        return "EmptyInput";
    case ErrorCode::InvalidCharacter:
        return "InvalidCharacter";
    case ErrorCode::UnexpectedEnd:
        return "UnexpectedEnd";
    case ErrorCode::TooManyDecimals:
        return "TooManyDecimals";
    case ErrorCode::Overflow:
        return "Overflow";
    case ErrorCode::InvalidLength:
        return "InvalidLength";
    case ErrorCode::InvalidCheckDigit:
        return "InvalidCheckDigit";
    case ErrorCode::RepeatedDigits:
        return "RepeatedDigits";
    case ErrorCode::ValueTooLong:
        return "ValueTooLong";
    case ErrorCode::MissingField:
        return "MissingField";
    case ErrorCode::InvalidValue:
        return "InvalidValue";
    case ErrorCode::TooManyFields:
        return "TooManyFields";
    case ErrorCode::IoError:
        return "IoError";
    }
    return "Unknown";
}

namespace detail {

constexpr std::unexpected<Error> fail(ErrorCode code, std::size_t offset, std::string_view detail) noexcept {
    return std::unexpected(Error{code, static_cast<std::uint64_t>(offset), detail});
}

constexpr bool isDigit(char c) noexcept { return c >= '0' && c <= '9'; }

}

}
