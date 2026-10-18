#pragma once

#include <celerpay/error.hpp>
#include <celerpay/export.hpp>

#include <array>
#include <span>
#include <string>
#include <string_view>

namespace celerpay {

namespace detail {
struct DocumentAccess;
}

class Cpf {
public:
    /// Vazio. Um Cpf válido só sai de parseCpf ou makeCpf.
    Cpf() noexcept = default;

    std::string_view digits() const noexcept { return {digits_.data(), digits_.size()}; }

    friend bool operator==(const Cpf&, const Cpf&) noexcept = default;

private:
    friend struct detail::DocumentAccess;

    std::array<char, 11> digits_{};
};

class Cnpj {
public:
    /// Vazio. Um Cnpj válido só sai de parseCnpj ou makeCnpj.
    Cnpj() noexcept = default;

    std::string_view chars() const noexcept { return {chars_.data(), chars_.size()}; }
    CELERPAY_API bool isAlphanumeric() const noexcept;

    friend bool operator==(const Cnpj&, const Cnpj&) noexcept = default;

private:
    friend struct detail::DocumentAccess;

    std::array<char, 14> chars_{};
};

/// Aceita "12345678909" ou "123.456.789-09".
CELERPAY_API Result<Cpf> parseCpf(std::string_view text) noexcept;
CELERPAY_API bool isValidCpf(std::string_view text) noexcept;
/// Calcula os dígitos verificadores a partir dos 9 dígitos da base.
CELERPAY_API Result<Cpf> makeCpf(std::string_view base) noexcept;
/// Formata como "123.456.789-09".
CELERPAY_API std::string formatCpf(const Cpf& cpf);

/// Aceita "12ABC34501DE35" ou "12.ABC.345/01DE-35". Letras minúsculas viram maiúsculas.
CELERPAY_API Result<Cnpj> parseCnpj(std::string_view text) noexcept;
CELERPAY_API bool isValidCnpj(std::string_view text) noexcept;
/// Calcula os dígitos verificadores a partir dos 12 caracteres da base.
CELERPAY_API Result<Cnpj> makeCnpj(std::string_view base) noexcept;
/// Formata como "12.ABC.345/01DE-35".
CELERPAY_API std::string formatCnpj(const Cnpj& cnpj);

/// parseCpf em lote, com caminho SIMD para entradas de 11 dígitos.
CELERPAY_API Result<void> parseCpfBatch(std::span<const std::string_view> inputs, std::span<Result<Cpf>> outputs) noexcept;
/// parseCnpj em lote, com caminho SIMD para entradas de 14 caracteres.
CELERPAY_API Result<void> parseCnpjBatch(std::span<const std::string_view> inputs, std::span<Result<Cnpj>> outputs) noexcept;
/// Kernel SIMD em uso: "avx2", "neon" ou "scalar".
CELERPAY_API const char* simdBackend() noexcept;

}
