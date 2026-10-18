#pragma once

#include <celerpay/error.hpp>

#include <cassert>
#include <cstddef>
#include <string_view>

namespace celerpay {

/// Módulo 10 com pesos 2, 1, 2, 1... da direita para a esquerda. Retorna o dígito verificador.
constexpr Result<int> mod10(std::string_view digits) noexcept {
    if (digits.empty()) {
        return detail::fail(ErrorCode::EmptyInput, 0, "entrada vazia");
    }
    int sum = 0;
    int weight = 2;
    for (std::size_t i = digits.size(); i-- > 0;) {
        if (!detail::isDigit(digits[i])) {
            return detail::fail(ErrorCode::InvalidCharacter, i, "esperado um dígito");
        }
        const int product = (digits[i] - '0') * weight;
        sum = (sum + product / 10 + product % 10) % 10;
        weight = 3 - weight;
    }
    return (10 - sum) % 10;
}

/// Soma ponderada módulo 11 com pesos 2..maxWeight da direita para a esquerda. Retorna o resto.
constexpr Result<int> mod11Remainder(std::string_view digits, int maxWeight = 9) noexcept {
    assert(maxWeight >= 2);
    if (digits.empty()) {
        return detail::fail(ErrorCode::EmptyInput, 0, "entrada vazia");
    }
    int sum = 0;
    int weight = 2;
    for (std::size_t i = digits.size(); i-- > 0;) {
        if (!detail::isDigit(digits[i])) {
            return detail::fail(ErrorCode::InvalidCharacter, i, "esperado um dígito");
        }
        sum = (sum + (digits[i] - '0') * weight) % 11;
        weight = weight == maxWeight ? 2 : weight + 1;
    }
    return sum;
}

}
