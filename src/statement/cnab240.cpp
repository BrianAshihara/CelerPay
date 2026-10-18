#include "statement/common.hpp"

#include <array>
#include <istream>
#include <string>

using namespace std;
using namespace std::chrono;

namespace celerpay::statement {

namespace {

using namespace detail;

constexpr size_t kRecord = 240;

constexpr cnab240::Layout kFebraban{
    {8, 1},
    {14, 1},
    {{16, 2}, {38, 20}, {59, 15}, {74, 8}, {82, 15}, {133, 1}, {134, 15}, {149, 40}, {199, 15}},
    {{16, 2}, {18, 15}, {33, 15}, {48, 15}, {78, 15}, {138, 8}, {146, 8}},
    {24, 6},
};

constexpr string_view field(string_view record, cnab240::Field f) noexcept { return record.substr(f.start - 1u, f.length); }

class Parser {
public:
    Parser(const cnab240::Layout& layout, const Sink& sink) : layout_{layout}, sink_{sink} {}

    Result<void> line(string_view text, uint64_t number) {
        if (!text.empty() && text.back() == '\r') {
            text.remove_suffix(1);
        }
        if (text.empty()) {
            return {};
        }
        if (finished_) {
            return fail(ErrorCode::InvalidValue, number, "registro depois do trailer de arquivo");
        }
        if (text.size() != kRecord) {
            return fail(ErrorCode::InvalidLength, number, "registro CNAB 240 deve ter 240 caracteres");
        }
        ++summary_.records;
        switch (field(text, layout_.recordType)[0]) {
        case '0':
        case '1':
        case '5':
            return pendingCheck(number);
        case '3':
            return detailRecord(text, number);
        case '9':
            return trailer(text, number);
        default:
            return fail(ErrorCode::InvalidValue, number, "tipo de registro desconhecido");
        }
    }

    Result<Summary> finish(uint64_t lastLine) {
        if (const auto pending = pendingCheck(lastLine); !pending) {
            return unexpected(pending.error());
        }
        if (!finished_) {
            return fail(ErrorCode::MissingField, lastLine, "arquivo sem trailer (registro 9)");
        }
        return summary_;
    }

private:
    Result<void> pendingCheck(uint64_t number) const {
        if (hasT_) {
            return fail(ErrorCode::MissingField, number, "segmento T sem o segmento U");
        }
        return {};
    }

    Result<void> detailRecord(string_view text, uint64_t number) {
        const char segment = field(text, layout_.segment)[0];
        if (segment == 'T') {
            if (const auto pending = pendingCheck(number); !pending) {
                return pending;
            }
            text.copy(t_.data(), kRecord);
            tLine_ = number;
            hasT_ = true;
            return {};
        }
        if (segment == 'U') {
            if (!hasT_) {
                return fail(ErrorCode::MissingField, number, "segmento U sem o segmento T");
            }
            hasT_ = false;
            return emit(string_view(t_.data(), kRecord), text, number);
        }
        return {};
    }

    Result<void> trailer(string_view text, uint64_t number) {
        if (const auto pending = pendingCheck(number); !pending) {
            return pending;
        }
        const auto count = parseDigits(field(text, layout_.fileRecordCount));
        if (!count || static_cast<uint64_t>(*count) != summary_.records % 1'000'000) {
            return fail(ErrorCode::InvalidValue, number, "quantidade de registros do trailer não confere");
        }
        finished_ = true;
        return {};
    }

    static Result<Money> money(string_view text, uint64_t number) {
        const auto value = parseDigits(text);
        if (!value) {
            return fail(ErrorCode::InvalidCharacter, number, "valor CNAB deve ser numérico");
        }
        return Money::fromCents(*value);
    }

    static Result<optional<year_month_day>> date(string_view text, uint64_t number) {
        if (text.find_first_not_of(" 0") == string_view::npos) {
            return optional<year_month_day>{};
        }
        const auto d = parseDigits(text.substr(0, 2));
        const auto m = parseDigits(text.substr(2, 2));
        const auto y = parseDigits(text.substr(4, 4));
        const auto parsed = d && m && y ? makeDate(*y, *m, *d) : nullopt;
        if (!parsed) {
            return fail(ErrorCode::InvalidValue, number, "data CNAB inválida (DDMMAAAA)");
        }
        return parsed;
    }

    Result<void> emit(string_view t, string_view u, uint64_t number) {
        const auto& lt = layout_.t;
        const auto& lu = layout_.u;
        Transaction tx;
        tx.source = Source::Cnab240;
        tx.position = tLine_;
        tx.movement = field(t, lt.movement);
        tx.id = trimRight(field(t, lt.ourNumber));
        tx.reference = trimRight(field(t, lt.documentNumber));
        tx.payerName = trimRight(field(t, lt.payerName));
        const string_view document = field(t, lt.payerDocument);
        switch (field(t, lt.payerType)[0]) {
        case '1':
            tx.payerDocument = document.substr(document.size() - 11);
            break;
        case '2':
            tx.payerDocument = document.substr(document.size() - 14);
            break;
        default:
            break;
        }

        const auto due = date(field(t, lt.dueDate), tLine_);
        const auto nominal = money(field(t, lt.nominal), tLine_);
        const auto fee = money(field(t, lt.fee), tLine_);
        const auto interest = money(field(u, lu.interest), number);
        const auto discount = money(field(u, lu.discount), number);
        const auto rebate = money(field(u, lu.rebate), number);
        const auto paid = money(field(u, lu.paid), number);
        const auto credit = date(field(u, lu.creditDate), number);
        const auto occurrence = date(field(u, lu.occurrenceDate), number);
        for (const auto* r : {&nominal, &fee, &interest, &discount, &rebate, &paid}) {
            if (!*r) {
                return unexpected(r->error());
            }
        }
        for (const auto* r : {&due, &credit, &occurrence}) {
            if (!*r) {
                return unexpected(r->error());
            }
        }
        const optional<year_month_day> when = credit->has_value() ? *credit : *occurrence;
        if (!when) {
            return fail(ErrorCode::MissingField, number, "segmento U sem data de crédito nem de ocorrência");
        }
        tx.dueDate = *due;
        tx.nominal = *nominal;
        tx.fee = *fee;
        tx.interest = *interest;
        tx.discount = Money::fromCents(discount->cents() + rebate->cents());
        tx.amount = *paid;
        tx.date = *when;
        if (const auto added = accumulate(summary_, tx.amount); !added) {
            return fail(ErrorCode::Overflow, number, "total do arquivo excede o limite");
        }
        sink_(tx);
        return {};
    }

    const cnab240::Layout& layout_;
    const Sink& sink_;
    Summary summary_;
    array<char, kRecord> t_{};
    uint64_t tLine_ = 0;
    bool hasT_ = false;
    bool finished_ = false;
};

}

const cnab240::Layout& cnab240::febraban() noexcept { return kFebraban; }

Result<Summary> parseCnab240(string_view data, const cnab240::Layout& layout, const Sink& sink) {
    Parser parser(layout, sink);
    uint64_t number = 0;
    for (size_t start = 0; start < data.size();) {
        const size_t end = data.find('\n', start);
        const string_view text = data.substr(start, end == string_view::npos ? string_view::npos : end - start);
        ++number;
        if (const auto handled = parser.line(text, number); !handled) {
            return unexpected(handled.error());
        }
        if (end == string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return parser.finish(number);
}

Result<Summary> parseCnab240(istream& input, const cnab240::Layout& layout, const Sink& sink) {
    Parser parser(layout, sink);
    string line;
    line.reserve(kRecord + 2);
    uint64_t number = 0;
    while (getline(input, line)) {
        ++number;
        if (line.size() > kRecord + 1) {
            return fail(ErrorCode::InvalidLength, number, "registro CNAB 240 deve ter 240 caracteres");
        }
        if (const auto handled = parser.line(line, number); !handled) {
            return unexpected(handled.error());
        }
    }
    if (input.bad()) {
        return fail(ErrorCode::IoError, number, "erro de leitura");
    }
    return parser.finish(number);
}

}
