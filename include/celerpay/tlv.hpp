#pragma once

#include <celerpay/error.hpp>
#include <celerpay/export.hpp>

#include <cstddef>
#include <string>
#include <string_view>

namespace celerpay {

struct TlvField {
    std::string_view id;
    std::string_view value;
    std::size_t offset = 0;
    std::size_t end = 0;
};

/// Lê um campo EMV (id de 2 dígitos, tamanho de 2 dígitos, valor) a partir de offset.
constexpr Result<TlvField> readTlv(std::string_view data, std::size_t offset) noexcept {
    for (std::size_t k = 0; k < 4; ++k) {
        const std::size_t pos = offset + k;
        if (pos >= data.size()) {
            return detail::fail(ErrorCode::UnexpectedEnd, pos, "cabeçalho TLV incompleto");
        }
        if (!detail::isDigit(data[pos])) {
            return detail::fail(ErrorCode::InvalidCharacter, pos,
                                k < 2 ? "id TLV deve ter 2 dígitos" : "tamanho TLV deve ter 2 dígitos");
        }
    }
    const auto length = static_cast<std::size_t>((data[offset + 2] - '0') * 10 + (data[offset + 3] - '0'));
    const std::size_t valueStart = offset + 4;
    if (length > data.size() - valueStart) {
        return detail::fail(ErrorCode::UnexpectedEnd, data.size(), "valor TLV incompleto");
    }
    return TlvField{data.substr(offset, 2), data.substr(valueStart, length), offset, valueStart + length};
}

CELERPAY_API Result<void> appendTlv(std::string& out, std::string_view id, std::string_view value);

}
