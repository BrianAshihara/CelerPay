#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace celerpay {

namespace detail {

inline constexpr std::uint16_t kCrc16Poly = 0x1021;
inline constexpr std::uint16_t kCrc16Init = 0xFFFF;

constexpr std::uint16_t crc16Step(std::uint16_t crc) noexcept {
    return (crc & 0x8000) != 0 ? static_cast<std::uint16_t>((crc << 1) ^ kCrc16Poly) : static_cast<std::uint16_t>(crc << 1);
}

constexpr std::uint16_t crc16Reference(std::string_view data) noexcept {
    std::uint16_t crc = kCrc16Init;
    for (const char c : data) {
        crc = static_cast<std::uint16_t>(crc ^ (static_cast<unsigned char>(c) << 8));
        for (int bit = 0; bit < 8; ++bit) {
            crc = crc16Step(crc);
        }
    }
    return crc;
}

inline constexpr std::array<std::uint16_t, 256> kCrc16Table = [] {
    std::array<std::uint16_t, 256> table{};
    for (std::size_t i = 0; i < table.size(); ++i) {
        auto crc = static_cast<std::uint16_t>(i << 8);
        for (int bit = 0; bit < 8; ++bit) {
            crc = crc16Step(crc);
        }
        table[i] = crc;
    }
    return table;
}();

}

/// CRC-16/CCITT-FALSE: polinômio 0x1021, inicial 0xFFFF. Usado no campo 63 do PIX.
constexpr std::uint16_t crc16Ccitt(std::string_view data) noexcept {
    std::uint16_t crc = detail::kCrc16Init;
    for (const char c : data) {
        const auto index = static_cast<std::uint8_t>((crc >> 8) ^ static_cast<unsigned char>(c));
        crc = static_cast<std::uint16_t>((crc << 8) ^ detail::kCrc16Table[index]);
    }
    return crc;
}

constexpr std::array<char, 4> crc16Hex(std::uint16_t crc) noexcept {
    constexpr std::string_view hex = "0123456789ABCDEF";
    return {hex[(crc >> 12) & 0xF], hex[(crc >> 8) & 0xF], hex[(crc >> 4) & 0xF], hex[crc & 0xF]};
}

}
