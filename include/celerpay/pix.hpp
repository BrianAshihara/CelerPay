#pragma once

#include <celerpay/error.hpp>
#include <celerpay/export.hpp>
#include <celerpay/money.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace celerpay::pix {

enum class KeyType { Cpf, Cnpj, Email, Phone, Random };

enum class Initiation { Unspecified, Reusable, SingleUse };

/// Campo que o CelerPay não interpreta. parent é "" no nível principal, ou "26" e "62" para subcampos.
struct RawField {
    std::string_view parent;
    std::string_view id;
    std::string_view value;

    friend bool operator==(const RawField&, const RawField&) noexcept = default;
};

inline constexpr std::size_t kMaxExtras = 32;

struct Payload {
    Initiation initiation = Initiation::Unspecified;
    std::string_view key;
    std::string_view description;
    std::string_view url;
    std::string_view merchantCategory = "0000";
    std::optional<Money> amount;
    std::string_view merchantName;
    std::string_view merchantCity;
    std::string_view postalCode;
    /// Vazio significa sem txid. O parse converte "***" em vazio e o build gera "***".
    std::string_view txid;

    std::span<const RawField> extras() const noexcept { return {extras_.data(), extraCount_}; }

    Result<void> addExtra(RawField field) noexcept {
        if (extraCount_ == kMaxExtras) {
            return detail::fail(ErrorCode::TooManyFields, 0, "mais de 32 campos desconhecidos");
        }
        extras_[extraCount_++] = field;
        return {};
    }

    bool isDynamic() const noexcept { return !url.empty(); }

    friend bool operator==(const Payload& a, const Payload& b) noexcept {
        return a.initiation == b.initiation && a.key == b.key && a.description == b.description && a.url == b.url &&
               a.merchantCategory == b.merchantCategory && a.amount == b.amount && a.merchantName == b.merchantName &&
               a.merchantCity == b.merchantCity && a.postalCode == b.postalCode && a.txid == b.txid &&
               std::ranges::is_permutation(a.extras(), b.extras());
    }

private:
    std::array<RawField, kMaxExtras> extras_{};
    std::size_t extraCount_ = 0;
};

CELERPAY_API Result<KeyType> validateKey(std::string_view key) noexcept;

/// Lê um BR Code PIX. Os campos de texto apontam para a entrada, que precisa continuar viva.
CELERPAY_API Result<Payload> parse(std::string_view payload) noexcept;
CELERPAY_API Result<void> parseBatch(std::span<const std::string_view> inputs, std::span<Result<Payload>> outputs) noexcept;

/// Monta o BR Code com CRC. Remove acentos de nome e cidade.
CELERPAY_API Result<std::string> build(const Payload& payload);

/// Troca letras acentuadas do Latin-1 pela letra sem acento. Outros caracteres fora do ASCII são erro.
CELERPAY_API Result<std::string> removeAccents(std::string_view text);

}
