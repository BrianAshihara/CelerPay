#pragma once

#include <celerpay/error.hpp>
#include <celerpay/export.hpp>
#include <celerpay/money.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iosfwd>
#include <optional>
#include <string_view>

namespace celerpay::statement {

enum class Source { Cnab240, Ofx, PixJson };

/// Pagamento recebido, normalizado. Os textos apontam para a entrada e valem só durante o callback.
struct Transaction {
    Source source = Source::Cnab240;
    Money amount;
    std::chrono::year_month_day date;
    std::optional<std::chrono::year_month_day> dueDate;
    std::string_view id;
    std::string_view reference;
    std::string_view endToEndId;
    std::string_view payerDocument;
    std::string_view payerName;
    std::string_view description;
    std::string_view movement;
    Money nominal;
    Money interest;
    Money discount;
    Money fee;
    /// Linha (CNAB) ou byte (OFX e JSON) onde a transação começa.
    std::uint64_t position = 0;
};

using Sink = std::function<void(const Transaction&)>;

struct Summary {
    std::uint64_t records = 0;
    std::uint64_t transactions = 0;
    Money total;
};

class MappedFile {
public:
    MappedFile() noexcept = default;
    CELERPAY_API MappedFile(MappedFile&& other) noexcept;
    CELERPAY_API MappedFile& operator=(MappedFile&& other) noexcept;
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;
    CELERPAY_API ~MappedFile();

    CELERPAY_API static Result<MappedFile> open(const std::filesystem::path& path);
    std::string_view data() const noexcept { return {data_, size_}; }

private:
    const char* data_ = nullptr;
    std::size_t size_ = 0;
    void* file_ = nullptr;
    void* mapping_ = nullptr;
};

namespace cnab240 {

/// Posição de um campo no registro de 240 bytes. start começa em 1, como nos manuais.
struct Field {
    std::uint16_t start = 0;
    std::uint16_t length = 0;
};

struct SegmentT {
    Field movement;
    Field ourNumber;
    Field documentNumber;
    Field dueDate;
    Field nominal;
    Field payerType;
    Field payerDocument;
    Field payerName;
    Field fee;
};

struct SegmentU {
    Field movement;
    Field interest;
    Field discount;
    Field rebate;
    Field paid;
    Field occurrenceDate;
    Field creditDate;
};

struct Layout {
    Field recordType;
    Field segment;
    SegmentT t;
    SegmentU u;
    Field fileRecordCount;
};

/// Layout padrão FEBRABAN de retorno de cobrança.
CELERPAY_API const Layout& febraban() noexcept;

}

CELERPAY_API Result<Summary> parseCnab240(std::string_view data, const cnab240::Layout& layout, const Sink& sink);
/// Lê linha a linha com buffer fixo. Memória constante para arquivos de qualquer tamanho.
CELERPAY_API Result<Summary> parseCnab240(std::istream& input, const cnab240::Layout& layout, const Sink& sink);
CELERPAY_API Result<Summary> parseOfx(std::string_view data, const Sink& sink);
CELERPAY_API Result<Summary> parsePixJson(std::string_view data, const Sink& sink);

}
