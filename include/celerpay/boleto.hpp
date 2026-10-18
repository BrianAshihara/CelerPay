#pragma once

#include <celerpay/error.hpp>
#include <celerpay/export.hpp>
#include <celerpay/money.hpp>
#include <celerpay/pix.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace celerpay::boleto {

enum class Kind { Bank, Collection };

namespace detail {
struct BarcodeAccess;
}

class Barcode {
public:
    /// Vazio. Um Barcode válido só sai de parse ou make.
    Barcode() noexcept = default;

    std::string_view digits() const noexcept { return {digits_.data(), digits_.size()}; }
    Kind kind() const noexcept { return digits_[0] == '8' ? Kind::Collection : Kind::Bank; }

    friend bool operator==(const Barcode&, const Barcode&) noexcept = default;

private:
    friend struct detail::BarcodeAccess;

    std::array<char, 44> digits_{};
};

struct BankFields {
    std::string_view bank;
    char currency = '9';
    std::uint16_t dueFactor = 0;
    /// Vazio quando o valor é livre (zeros no código de barras).
    std::optional<Money> amount;
    std::string_view freeField;
};

struct CollectionFields {
    char segment = '1';
    /// '6' ou '7' para módulo 10, '8' ou '9' para módulo 11. '6' e '8' indicam valor em centavos.
    char valueType = '6';
    std::int64_t value = 0;
    std::string_view companyAndFree;
};

CELERPAY_API Result<Barcode> parseBarcode(std::string_view text) noexcept;
/// Aceita linha digitável só com dígitos (47 ou 48) ou com a máscara padrão.
CELERPAY_API Result<Barcode> parseLine(std::string_view text) noexcept;
/// Decide entre código de barras e linha digitável pelo tamanho.
CELERPAY_API Result<Barcode> parse(std::string_view text) noexcept;
CELERPAY_API std::string formatLine(const Barcode& barcode, bool masked = true);
CELERPAY_API Result<void> parseBatch(std::span<const std::string_view> inputs, std::span<Result<Barcode>> outputs) noexcept;

/// Campos de um boleto bancário. Os textos apontam para o barcode.
CELERPAY_API BankFields bankFields(const Barcode& barcode) noexcept;
/// Campos de um boleto de arrecadação. Os textos apontam para o barcode.
CELERPAY_API CollectionFields collectionFields(const Barcode& barcode) noexcept;
CELERPAY_API Result<Barcode> makeBankBarcode(const BankFields& fields) noexcept;
CELERPAY_API Result<Barcode> makeCollectionBarcode(const CollectionFields& fields) noexcept;

/// Data do fator no ciclo mais próximo da referência. Vazio para fator 0.
CELERPAY_API std::optional<std::chrono::year_month_day> dueDate(std::uint16_t factor,
                                                                std::chrono::year_month_day reference) noexcept;
CELERPAY_API Result<std::uint16_t> dueFactor(std::chrono::year_month_day date) noexcept;

struct Hybrid {
    Barcode barcode;
    pix::Payload pix;
};

/// Boleto bancário com QR PIX. O pix aponta para pixPayload.
CELERPAY_API Result<Hybrid> parseHybrid(std::string_view boleto, std::string_view pixPayload) noexcept;

}
