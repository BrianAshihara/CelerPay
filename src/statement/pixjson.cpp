#include "statement/common.hpp"

#include <array>
#include <string>

using namespace std;
using namespace std::chrono;

namespace celerpay::statement {

namespace {

using namespace detail;

constexpr int kMaxDepth = 64;

constexpr bool isHex(char c) noexcept { return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

class Json {
public:
    explicit Json(string_view text) noexcept : text_{text} {}

    size_t pos() const noexcept { return pos_; }
    size_t offsetOf(string_view part) const noexcept { return static_cast<size_t>(part.data() - text_.data()); }

    void whitespace() noexcept {
        while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\n' || text_[pos_] == '\r')) {
            ++pos_;
        }
    }

    bool peek(char c) noexcept {
        whitespace();
        return pos_ < text_.size() && text_[pos_] == c;
    }

    Result<void> expect(char c) noexcept {
        whitespace();
        if (pos_ >= text_.size()) {
            return fail(ErrorCode::UnexpectedEnd, pos_, "JSON terminou antes do esperado");
        }
        if (text_[pos_] != c) {
            return fail(ErrorCode::InvalidCharacter, pos_, "caractere inesperado no JSON");
        }
        ++pos_;
        return {};
    }

    Result<string_view> string() noexcept {
        if (const auto open = expect('"'); !open) {
            return unexpected(open.error());
        }
        const size_t start = pos_;
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c == '"') {
                return text_.substr(start, pos_++ - start);
            }
            if (static_cast<unsigned char>(c) < 0x20) {
                return fail(ErrorCode::InvalidCharacter, pos_, "caractere de controle em string JSON");
            }
            if (c == '\\') {
                if (++pos_ >= text_.size()) {
                    break;
                }
                const char e = text_[pos_];
                if (e == 'u') {
                    for (int k = 0; k < 4; ++k) {
                        if (++pos_ >= text_.size() || !isHex(text_[pos_])) {
                            return fail(ErrorCode::InvalidCharacter, pos_, "escape \\u inválido");
                        }
                    }
                } else if (string_view("\"\\/bfnrt").find(e) == string_view::npos) {
                    return fail(ErrorCode::InvalidCharacter, pos_, "escape inválido em string JSON");
                }
            }
            ++pos_;
        }
        return fail(ErrorCode::UnexpectedEnd, pos_, "string JSON sem fechamento");
    }

    Result<void> skip(int depth = 0) noexcept {
        if (depth > kMaxDepth) {
            return fail(ErrorCode::InvalidValue, pos_, "JSON aninhado demais");
        }
        whitespace();
        if (pos_ >= text_.size()) {
            return fail(ErrorCode::UnexpectedEnd, pos_, "JSON terminou antes do esperado");
        }
        const char c = text_[pos_];
        if (c == '"') {
            return string().transform([](string_view) {});
        }
        if (c == '{' || c == '[') {
            const char close = c == '{' ? '}' : ']';
            ++pos_;
            if (peek(close)) {
                ++pos_;
                return {};
            }
            for (;;) {
                if (c == '{') {
                    if (const auto key = string(); !key) {
                        return unexpected(key.error());
                    }
                    if (const auto colon = expect(':'); !colon) {
                        return colon;
                    }
                }
                if (const auto value = skip(depth + 1); !value) {
                    return value;
                }
                if (peek(',')) {
                    ++pos_;
                    continue;
                }
                return expect(close);
            }
        }
        for (const string_view literal : {"true", "false", "null"}) {
            if (text_.substr(pos_, literal.size()) == literal) {
                pos_ += literal.size();
                return {};
            }
        }
        return number();
    }

    template <class OnField>
    Result<void> object(OnField&& onField) noexcept {
        if (const auto open = expect('{'); !open) {
            return open;
        }
        if (peek('}')) {
            ++pos_;
            return {};
        }
        for (;;) {
            const auto key = string();
            if (!key) {
                return unexpected(key.error());
            }
            if (const auto colon = expect(':'); !colon) {
                return colon;
            }
            if (const auto value = onField(*key); !value) {
                return value;
            }
            if (peek(',')) {
                ++pos_;
                continue;
            }
            return expect('}');
        }
    }

    bool atEnd() noexcept {
        whitespace();
        return pos_ == text_.size();
    }

private:
    Result<void> number() noexcept {
        const size_t start = pos_;
        auto digits = [&] {
            const size_t begin = pos_;
            while (pos_ < text_.size() && isDigit(text_[pos_])) {
                ++pos_;
            }
            return pos_ > begin;
        };
        if (pos_ < text_.size() && text_[pos_] == '-') {
            ++pos_;
        }
        if (pos_ < text_.size() && text_[pos_] == '0') {
            ++pos_;
        } else if (!digits()) {
            return fail(ErrorCode::InvalidCharacter, start, "valor JSON inválido");
        }
        if (pos_ < text_.size() && text_[pos_] == '.') {
            ++pos_;
            if (!digits()) {
                return fail(ErrorCode::InvalidCharacter, pos_, "número JSON inválido");
            }
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) {
                ++pos_;
            }
            if (!digits()) {
                return fail(ErrorCode::InvalidCharacter, pos_, "número JSON inválido");
            }
        }
        return {};
    }

    string_view text_;
    size_t pos_ = 0;
};

Result<year_month_day> parseHorario(string_view text, size_t offset) noexcept {
    const auto y = parseDigits(text.substr(0, 4));
    const auto m = text.size() >= 7 && text[4] == '-' ? parseDigits(text.substr(5, 2)) : nullopt;
    const auto d = text.size() >= 10 && text[7] == '-' ? parseDigits(text.substr(8, 2)) : nullopt;
    const auto date = y && m && d ? makeDate(*y, *m, *d) : nullopt;
    if (!date) {
        return fail(ErrorCode::InvalidValue, offset, "horario deve começar com AAAA-MM-DD");
    }
    return *date;
}

Result<void> parsePix(Json& json, const Sink& sink, Summary& summary, array<string, 5>& scratch) {
    const size_t start = json.pos();
    Transaction tx;
    tx.source = Source::PixJson;
    string_view valor;
    string_view horario;
    auto text = [&](string_view& target) -> Result<void> {
        const auto value = json.string();
        if (!value) {
            return unexpected(value.error());
        }
        target = *value;
        return {};
    };
    const auto parsed = json.object([&](string_view key) -> Result<void> {
        if (key == "endToEndId") {
            return text(tx.endToEndId);
        }
        if (key == "txid") {
            return text(tx.id);
        }
        if (key == "infoPagador") {
            return text(tx.reference);
        }
        if (key == "valor") {
            return text(valor);
        }
        if (key == "horario") {
            return text(horario);
        }
        if (key == "pagador") {
            return json.object([&](string_view field) -> Result<void> {
                if (field == "cpf" || field == "cnpj") {
                    return text(tx.payerDocument);
                }
                if (field == "nome") {
                    return text(tx.payerName);
                }
                return json.skip(1);
            });
        }
        return json.skip(1);
    });
    if (!parsed) {
        return parsed;
    }
    ++summary.records;
    if (tx.endToEndId.empty() || valor.empty() || horario.empty()) {
        return fail(ErrorCode::MissingField, start, "pix sem endToEndId, valor ou horario");
    }
    const auto amount = parseAmount(valor);
    if (!amount) {
        return fail(amount.error().code, json.offsetOf(valor) + amount.error().offset, amount.error().detail);
    }
    const auto date = parseHorario(horario, json.offsetOf(horario));
    if (!date) {
        return unexpected(date.error());
    }
    tx.amount = *amount;
    tx.date = *date;
    tx.position = start;
    tx.endToEndId = decodeJson(tx.endToEndId, scratch[0]);
    tx.id = decodeJson(tx.id, scratch[1]);
    tx.reference = decodeJson(tx.reference, scratch[2]);
    tx.payerDocument = decodeJson(tx.payerDocument, scratch[3]);
    tx.payerName = decodeJson(tx.payerName, scratch[4]);
    if (const auto added = accumulate(summary, tx.amount); !added) {
        return fail(ErrorCode::Overflow, start, "total do arquivo excede o limite");
    }
    sink(tx);
    return {};
}

}

Result<Summary> parsePixJson(string_view data, const Sink& sink) {
    Json json(data);
    Summary summary;
    array<string, 5> scratch;
    bool hasPix = false;
    const auto parsed = json.object([&](string_view key) -> Result<void> {
        if (key != "pix") {
            return json.skip(1);
        }
        hasPix = true;
        if (const auto open = json.expect('['); !open) {
            return open;
        }
        if (json.peek(']')) {
            return json.expect(']');
        }
        for (;;) {
            json.whitespace();
            if (const auto item = parsePix(json, sink, summary, scratch); !item) {
                return item;
            }
            if (json.peek(',')) {
                (void)json.expect(',');
                continue;
            }
            return json.expect(']');
        }
    });
    if (!parsed) {
        return unexpected(parsed.error());
    }
    if (!json.atEnd()) {
        return fail(ErrorCode::InvalidCharacter, json.pos(), "conteúdo depois do fim do JSON");
    }
    if (!hasPix) {
        return fail(ErrorCode::MissingField, 0, "JSON sem o array pix");
    }
    return summary;
}

}
