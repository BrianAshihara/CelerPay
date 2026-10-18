#pragma once

#include <celerpay/statement.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <string>
#include <cstdint>
#include <optional>
#include <string_view>

namespace celerpay::statement::detail {

using celerpay::detail::fail;
using celerpay::detail::isDigit;

constexpr std::string_view trimRight(std::string_view text) noexcept {
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == '\n')) {
        text.remove_suffix(1);
    }
    return text;
}

constexpr std::string_view trim(std::string_view text) noexcept {
    text = trimRight(text);
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' || text.front() == '\n')) {
        text.remove_prefix(1);
    }
    return text;
}

constexpr std::optional<std::int64_t> parseDigits(std::string_view text) noexcept {
    if (text.empty() || text.size() > 18) {
        return std::nullopt;
    }
    std::int64_t value = 0;
    for (const char c : text) {
        if (!isDigit(c)) {
            return std::nullopt;
        }
        value = value * 10 + (c - '0');
    }
    return value;
}

constexpr std::optional<std::chrono::year_month_day> makeDate(std::int64_t y, std::int64_t m, std::int64_t d) noexcept {
    const std::chrono::year_month_day date{std::chrono::year{static_cast<int>(y)}, std::chrono::month{static_cast<unsigned>(m)},
                                           std::chrono::day{static_cast<unsigned>(d)}};
    if (!date.ok()) {
        return std::nullopt;
    }
    return date;
}

inline void appendUtf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

constexpr std::uint32_t hexValue(std::string_view text) noexcept {
    std::uint32_t value = 0;
    for (const char c : text) {
        const std::uint32_t digit = c <= '9' ? static_cast<std::uint32_t>(c - '0')
                                             : static_cast<std::uint32_t>((c | 0x20) - 'a' + 10);
        value = value * 16 + digit;
    }
    return value;
}

/// Decodifica uma string JSON já validada (sem as aspas). Devolve a própria entrada se não houver escape.
inline std::string_view decodeJson(std::string_view raw, std::string& out) {
    if (raw.find('\\') == std::string_view::npos) {
        return raw;
    }
    out.clear();
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] != '\\') {
            out.push_back(raw[i]);
            continue;
        }
        const char e = raw[++i];
        switch (e) {
        case 'b':
            out.push_back('\b');
            break;
        case 'f':
            out.push_back('\f');
            break;
        case 'n':
            out.push_back('\n');
            break;
        case 'r':
            out.push_back('\r');
            break;
        case 't':
            out.push_back('\t');
            break;
        case 'u': {
            std::uint32_t cp = hexValue(raw.substr(i + 1, 4));
            i += 4;
            if (cp >= 0xD800 && cp <= 0xDBFF && raw.substr(i + 1, 2) == "\\u") {
                const std::uint32_t low = hexValue(raw.substr(i + 3, 4));
                if (low >= 0xDC00 && low <= 0xDFFF) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    i += 6;
                }
            }
            appendUtf8(out, cp >= 0xD800 && cp <= 0xDFFF ? 0xFFFD : cp);
            break;
        }
        default:
            out.push_back(e);
            break;
        }
    }
    return out;
}

inline constexpr std::array<std::uint16_t, 32> kCp1252 = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
};

/// Decodifica entidades XML/SGML (&amp;, &#233;, &#xE9;) e, se latin1, converte Windows-1252 para UTF-8.
/// Devolve a própria entrada se não houver nada a converter.
inline std::string_view decodeOfx(std::string_view raw, bool latin1, std::string& out) {
    const bool high = latin1 && std::any_of(raw.begin(), raw.end(), [](char c) { return static_cast<unsigned char>(c) >= 0x80; });
    if (!high && raw.find('&') == std::string_view::npos) {
        return raw;
    }
    out.clear();
    for (std::size_t i = 0; i < raw.size(); ++i) {
        const auto byte = static_cast<unsigned char>(raw[i]);
        if (raw[i] == '&') {
            const std::size_t semi = raw.find(';', i);
            const std::string_view entity = semi == std::string_view::npos ? std::string_view{} : raw.substr(i + 1, semi - i - 1);
            std::uint32_t cp = 0;
            if (entity == "amp") {
                cp = '&';
            } else if (entity == "lt") {
                cp = '<';
            } else if (entity == "gt") {
                cp = '>';
            } else if (entity == "quot") {
                cp = '"';
            } else if (entity == "apos") {
                cp = '\'';
            } else if (entity.size() >= 2 && entity.size() <= 8 && entity[0] == '#') {
                const bool hex = entity[1] == 'x' || entity[1] == 'X';
                const std::string_view digits = entity.substr(hex ? 2 : 1);
                const bool ok = !digits.empty() && std::all_of(digits.begin(), digits.end(), [&](char c) {
                    return isDigit(c) || (hex && ((c | 0x20) >= 'a' && (c | 0x20) <= 'f'));
                });
                if (ok) {
                    cp = hex ? hexValue(digits) : static_cast<std::uint32_t>(*parseDigits(digits));
                }
            }
            if (cp != 0 && cp <= 0x10FFFF) {
                appendUtf8(out, cp >= 0xD800 && cp <= 0xDFFF ? 0xFFFD : cp);
                i = semi;
                continue;
            }
            out.push_back('&');
        } else if (latin1 && byte >= 0x80) {
            appendUtf8(out, byte < 0xA0 ? kCp1252[byte - 0x80] : byte);
        } else {
            out.push_back(raw[i]);
        }
    }
    return out;
}

/// Soma o valor no total do resumo, com checagem de estouro.
inline Result<void> accumulate(Summary& summary, Money amount) noexcept {
    const auto total = checkedAdd(summary.total, amount);
    if (!total) {
        return std::unexpected(total.error());
    }
    summary.total = *total;
    ++summary.transactions;
    return {};
}

}
