#include <celerpay/money.hpp>

#include <array>

using namespace std;

namespace celerpay {

namespace {

constexpr int64_t kMaxWhole = detail::kMaxCents / 100;
constexpr int64_t kMaxLastCents = detail::kMaxCents % 100;

using detail::fail;
using detail::isDigit;

}

Result<Money> parseAmount(string_view text) noexcept {
    if (text.empty()) {
        return fail(ErrorCode::EmptyInput, 0, "entrada vazia");
    }

    size_t i = 0;
    int64_t whole = 0;
    while (i < text.size() && isDigit(text[i])) {
        const int64_t digit = text[i] - '0';
        if (whole > (kMaxWhole - digit) / 10) {
            return fail(ErrorCode::Overflow, i, "valor excede o limite de int64 em centavos");
        }
        whole = whole * 10 + digit;
        ++i;
    }

    if (i == 0) {
        return fail(ErrorCode::InvalidCharacter, 0, "esperado um dígito");
    }
    if (i == text.size()) {
        return Money::fromCents(whole * 100);
    }
    if (text[i] != '.') {
        return fail(ErrorCode::InvalidCharacter, i, "esperado um dígito ou '.'");
    }

    ++i;
    if (i == text.size()) {
        return fail(ErrorCode::UnexpectedEnd, i, "esperado um dígito após o '.'");
    }

    int64_t fraction = 0;
    int decimals = 0;
    for (; i < text.size(); ++i) {
        if (!isDigit(text[i])) {
            return fail(ErrorCode::InvalidCharacter, i, "esperado um dígito");
        }
        if (decimals == 2) {
            return fail(ErrorCode::TooManyDecimals, i, "mais de duas casas decimais");
        }
        fraction = fraction * 10 + (text[i] - '0');
        ++decimals;
    }
    if (decimals == 1) {
        fraction *= 10;
    }

    if (whole == kMaxWhole && fraction > kMaxLastCents) {
        return fail(ErrorCode::Overflow, text.size() - 1, "valor excede o limite de int64 em centavos");
    }
    return Money::fromCents(whole * 100 + fraction);
}

string formatBrl(Money value) {
    const int64_t cents = value.cents();
    const uint64_t magnitude = cents < 0 ? 0 - static_cast<uint64_t>(cents) : static_cast<uint64_t>(cents);
    uint64_t whole = magnitude / 100;
    const uint64_t fraction = magnitude % 100;

    array<char, 40> buffer{};
    size_t pos = buffer.size();
    buffer[--pos] = static_cast<char>('0' + fraction % 10);
    buffer[--pos] = static_cast<char>('0' + fraction / 10);
    buffer[--pos] = ',';

    int groupDigits = 0;
    do {
        if (groupDigits == 3) {
            buffer[--pos] = '.';
            groupDigits = 0;
        }
        buffer[--pos] = static_cast<char>('0' + whole % 10);
        whole /= 10;
        ++groupDigits;
    } while (whole != 0);

    buffer[--pos] = ' ';
    buffer[--pos] = '$';
    buffer[--pos] = 'R';
    if (cents < 0) {
        buffer[--pos] = '-';
    }
    return string(buffer.data() + pos, buffer.size() - pos);
}

}
