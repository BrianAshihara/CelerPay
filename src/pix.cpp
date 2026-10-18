#include <celerpay/pix.hpp>

#include <celerpay/crc16.hpp>
#include <celerpay/document.hpp>
#include <celerpay/tlv.hpp>

#include <algorithm>
#include <vector>

using namespace std;

namespace celerpay::pix {

namespace {

using detail::fail;
using detail::isDigit;

constexpr string_view kGui = "br.gov.bcb.pix";
constexpr string_view kNoTxid = "***";
constexpr size_t kMaxName = 25;
constexpr size_t kMaxCity = 15;
constexpr size_t kMaxTxid = 25;
constexpr size_t kMaxUrl = 77;
constexpr size_t kMaxEmail = 77;
constexpr size_t kMaxAmount = 13;

constexpr string_view kAccentMap = "AAAAAA-CEEEEIIII-NOOOOO--UUUUY--aaaaaa-ceeeeiiii-nooooo--uuuuy-y";

constexpr bool isAlnum(char c) noexcept { return isDigit(c) || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }

constexpr bool isHex(char c) noexcept { return isDigit(c) || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'); }

constexpr char toLower(char c) noexcept { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

constexpr bool equalsIgnoreCase(string_view a, string_view b) noexcept {
    return a.size() == b.size() && equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return toLower(x) == toLower(y); });
}

constexpr bool allOf(string_view text, bool (*predicate)(char) noexcept) noexcept {
    return all_of(text.begin(), text.end(), predicate);
}

constexpr bool isPrintable(char c) noexcept { return c >= 0x20 && c <= 0x7E; }

unexpected<Error> rebase(Error error, size_t base) noexcept {
    error.offset += base;
    return unexpected(error);
}

constexpr int fieldNumber(string_view id) noexcept { return (id[0] - '0') * 10 + (id[1] - '0'); }

bool isEmailLocalChar(char c) noexcept {
    return isAlnum(c) || string_view(".!#$%&'*+/=?^_`{|}~-").find(c) != string_view::npos;
}

Result<KeyType> validateEmail(string_view key) noexcept {
    if (key.size() > kMaxEmail) {
        return fail(ErrorCode::ValueTooLong, kMaxEmail, "e-mail acima de 77 caracteres");
    }
    const size_t at = key.find('@');
    if (at == 0 || key.find('@', at + 1) != string_view::npos) {
        return fail(ErrorCode::InvalidValue, at, "e-mail deve ter uma única '@' com parte local");
    }
    for (size_t i = 0; i < at; ++i) {
        if (!isEmailLocalChar(key[i])) {
            return fail(ErrorCode::InvalidCharacter, i, "caractere inválido no e-mail");
        }
    }
    const string_view domain = key.substr(at + 1);
    if (domain.find('.') == string_view::npos) {
        return fail(ErrorCode::InvalidValue, at + 1, "domínio do e-mail sem '.'");
    }
    size_t labelStart = 0;
    for (size_t i = 0; i <= domain.size(); ++i) {
        if (i == domain.size() || domain[i] == '.') {
            const string_view label = domain.substr(labelStart, i - labelStart);
            if (label.empty() || label.front() == '-' || label.back() == '-') {
                return fail(ErrorCode::InvalidValue, at + 1 + labelStart, "rótulo de domínio inválido no e-mail");
            }
            labelStart = i + 1;
        } else if (!isAlnum(domain[i]) && domain[i] != '-') {
            return fail(ErrorCode::InvalidCharacter, at + 1 + i, "caractere inválido no domínio do e-mail");
        }
    }
    return KeyType::Email;
}

Result<KeyType> validatePhone(string_view key) noexcept {
    if (!key.starts_with("+55")) {
        return fail(ErrorCode::InvalidValue, 0, "telefone deve começar com +55");
    }
    const string_view number = key.substr(3);
    if (number.size() != 10 && number.size() != 11) {
        return fail(ErrorCode::InvalidLength, 3, "telefone deve ter DDD e 8 ou 9 dígitos");
    }
    for (size_t i = 0; i < number.size(); ++i) {
        if (!isDigit(number[i])) {
            return fail(ErrorCode::InvalidCharacter, 3 + i, "esperado um dígito");
        }
    }
    if (number[0] == '0' || number[1] == '0') {
        return fail(ErrorCode::InvalidValue, 3, "DDD inválido");
    }
    return KeyType::Phone;
}

Result<KeyType> validateUuid(string_view key) noexcept {
    for (size_t i = 0; i < key.size(); ++i) {
        const bool dash = i == 8 || i == 13 || i == 18 || i == 23;
        if (dash ? key[i] != '-' : !isHex(key[i])) {
            return fail(ErrorCode::InvalidCharacter, i, "chave aleatória deve ser um UUID");
        }
    }
    return KeyType::Random;
}

Result<void> validateUrl(string_view url) noexcept {
    if (url.size() > kMaxUrl) {
        return fail(ErrorCode::ValueTooLong, kMaxUrl, "URL acima de 77 caracteres");
    }
    if (url.starts_with("http://") || url.starts_with("https://")) {
        return fail(ErrorCode::InvalidValue, 0, "URL do PIX dinâmico não leva o esquema");
    }
    for (size_t i = 0; i < url.size(); ++i) {
        if (!isPrintable(url[i]) || url[i] == ' ') {
            return fail(ErrorCode::InvalidCharacter, i, "caractere inválido na URL");
        }
    }
    const size_t slash = url.find('/');
    const string_view host = url.substr(0, slash);
    if (host.empty() || host.find('.') == string_view::npos || slash == string_view::npos || slash + 1 == url.size()) {
        return fail(ErrorCode::InvalidValue, 0, "URL deve ter host e caminho");
    }
    return {};
}

Result<void> validateTxid(string_view txid) noexcept {
    if (txid == kNoTxid) {
        return {};
    }
    if (txid.size() > kMaxTxid) {
        return fail(ErrorCode::ValueTooLong, kMaxTxid, "txid acima de 25 caracteres");
    }
    for (size_t i = 0; i < txid.size(); ++i) {
        if (!isAlnum(txid[i])) {
            return fail(ErrorCode::InvalidCharacter, i, "txid aceita apenas letras e dígitos");
        }
    }
    return {};
}

Result<void> validateText(string_view text, size_t maxLength, string_view emptyDetail, string_view longDetail) noexcept {
    if (text.empty()) {
        return fail(ErrorCode::MissingField, 0, emptyDetail);
    }
    if (text.size() > maxLength) {
        return fail(ErrorCode::ValueTooLong, maxLength, longDetail);
    }
    for (size_t i = 0; i < text.size(); ++i) {
        if (!isPrintable(text[i])) {
            return fail(ErrorCode::InvalidCharacter, i, "caractere fora do ASCII imprimível");
        }
    }
    return {};
}

Result<void> validateDigits(string_view text, size_t length, string_view detail) noexcept {
    if (text.size() != length) {
        return fail(ErrorCode::InvalidLength, 0, detail);
    }
    for (size_t i = 0; i < text.size(); ++i) {
        if (!isDigit(text[i])) {
            return fail(ErrorCode::InvalidCharacter, i, detail);
        }
    }
    return {};
}

Result<void> validateAccount(const Payload& p, size_t keyOffset, size_t urlOffset) noexcept {
    if (p.key.empty() == p.url.empty()) {
        return fail(ErrorCode::InvalidValue, 0, "payload deve ter chave (estático) ou URL (dinâmico), nunca os dois");
    }
    if (!p.key.empty()) {
        if (const auto key = validateKey(p.key); !key) {
            return rebase(key.error(), keyOffset);
        }
    }
    if (!p.url.empty()) {
        if (const auto url = validateUrl(p.url); !url) {
            return rebase(url.error(), urlOffset);
        }
    }
    return {};
}

Result<void> validateAmount(Money amount) noexcept {
    if (amount.cents() <= 0) {
        return fail(ErrorCode::InvalidValue, 0, "valor deve ser maior que zero");
    }
    return {};
}

template <class Handler>
Result<void> forEachSubfield(string_view data, const TlvField& parent, Handler&& handler) noexcept {
    const string_view scope = data.substr(0, parent.end);
    array<bool, 100> seen{};
    for (size_t offset = parent.offset + 4; offset < parent.end;) {
        const auto field = readTlv(scope, offset);
        if (!field) {
            return unexpected(field.error());
        }
        const int number = fieldNumber(field->id);
        if (seen[static_cast<size_t>(number)]) {
            return fail(ErrorCode::InvalidValue, offset, "subcampo repetido");
        }
        seen[static_cast<size_t>(number)] = true;
        if (const auto handled = handler(number, *field); !handled) {
            return handled;
        }
        offset = field->end;
    }
    return {};
}

string formatAmount(Money amount) {
    const int64_t cents = amount.cents();
    const int64_t fraction = cents % 100;
    return to_string(cents / 100) + (fraction < 10 ? ".0" : ".") + to_string(fraction);
}

struct Entry {
    string_view id;
    string value;
};

Result<void> appendSorted(string& out, vector<Entry>& entries) {
    stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.id < b.id; });
    for (size_t i = 0; i < entries.size(); ++i) {
        if (i > 0 && entries[i].id == entries[i - 1].id) {
            return fail(ErrorCode::InvalidValue, 0, "campo extra repete um id já usado");
        }
        if (const auto appended = appendTlv(out, entries[i].id, entries[i].value); !appended) {
            return appended;
        }
    }
    return {};
}

}

Result<KeyType> validateKey(string_view key) noexcept {
    if (key.empty()) {
        return fail(ErrorCode::EmptyInput, 0, "chave vazia");
    }
    if (key[0] == '+') {
        return validatePhone(key);
    }
    if (key.find('@') != string_view::npos) {
        return validateEmail(key);
    }
    if (key.size() == 36) {
        return validateUuid(key);
    }
    if (key.size() == 11 && allOf(key, isDigit)) {
        if (const auto cpf = parseCpf(key); !cpf) {
            return unexpected(cpf.error());
        }
        return KeyType::Cpf;
    }
    if (key.size() == 14) {
        if (const auto cnpj = parseCnpj(key); !cnpj) {
            return unexpected(cnpj.error());
        }
        return KeyType::Cnpj;
    }
    return fail(ErrorCode::InvalidValue, 0, "chave não é CPF, CNPJ, e-mail, telefone nem UUID");
}

Result<Payload> parse(string_view data) noexcept {
    if (data.empty()) {
        return fail(ErrorCode::EmptyInput, 0, "payload vazio");
    }

    Payload out;
    out.merchantCategory = {};
    array<bool, 100> seen{};
    size_t keyOffset = 0;
    size_t urlOffset = 0;
    size_t amountOffset = 0;
    size_t txidOffset = 0;
    array<size_t, 100> offsets{};

    for (size_t offset = 0; offset < data.size();) {
        const auto field = readTlv(data, offset);
        if (!field) {
            return unexpected(field.error());
        }
        const int number = fieldNumber(field->id);
        if (seen[static_cast<size_t>(number)]) {
            return fail(ErrorCode::InvalidValue, offset, "campo repetido");
        }
        if (offset == 0 && number != 0) {
            return fail(ErrorCode::MissingField, 0, "campo 00 deve ser o primeiro");
        }
        seen[static_cast<size_t>(number)] = true;
        offsets[static_cast<size_t>(number)] = offset + 4;
        const string_view value = field->value;

        switch (number) {
        case 0:
            if (value != "01") {
                return fail(ErrorCode::InvalidValue, offset + 4, "campo 00 deve ser 01");
            }
            break;
        case 1:
            if (value == "11") {
                out.initiation = Initiation::Reusable;
            } else if (value == "12") {
                out.initiation = Initiation::SingleUse;
            } else {
                return fail(ErrorCode::InvalidValue, offset + 4, "campo 01 deve ser 11 ou 12");
            }
            break;
        case 26: {
            bool hasGui = false;
            const auto sub = forEachSubfield(data, *field, [&](int id, const TlvField& f) -> Result<void> {
                switch (id) {
                case 0:
                    if (!equalsIgnoreCase(f.value, kGui)) {
                        return fail(ErrorCode::InvalidValue, f.offset + 4, "GUI deve ser br.gov.bcb.pix");
                    }
                    hasGui = true;
                    return {};
                case 1:
                    out.key = f.value;
                    keyOffset = f.offset + 4;
                    return {};
                case 2:
                    out.description = f.value;
                    return {};
                case 25:
                    out.url = f.value;
                    urlOffset = f.offset + 4;
                    return {};
                default:
                    return out.addExtra({"26", f.id, f.value});
                }
            });
            if (!sub) {
                return unexpected(sub.error());
            }
            if (!hasGui) {
                return fail(ErrorCode::MissingField, offset + 4, "campo 26 sem GUI br.gov.bcb.pix");
            }
            break;
        }
        case 52:
            if (const auto mcc = validateDigits(value, 4, "MCC deve ter 4 dígitos"); !mcc) {
                return rebase(mcc.error(), offset + 4);
            }
            out.merchantCategory = value;
            break;
        case 53:
            if (value != "986") {
                return fail(ErrorCode::InvalidValue, offset + 4, "moeda deve ser 986 (BRL)");
            }
            break;
        case 54: {
            if (value.size() > kMaxAmount) {
                return fail(ErrorCode::ValueTooLong, offset + 4 + kMaxAmount, "valor acima de 13 caracteres");
            }
            const auto amount = parseAmount(value);
            if (!amount) {
                return rebase(amount.error(), offset + 4);
            }
            out.amount = *amount;
            amountOffset = offset + 4;
            break;
        }
        case 58:
            if (value != "BR") {
                return fail(ErrorCode::InvalidValue, offset + 4, "país deve ser BR");
            }
            break;
        case 59:
            out.merchantName = value;
            break;
        case 60:
            out.merchantCity = value;
            break;
        case 61:
            if (const auto cep = validateDigits(value, 8, "CEP deve ter 8 dígitos"); !cep) {
                return rebase(cep.error(), offset + 4);
            }
            out.postalCode = value;
            break;
        case 62: {
            const auto sub = forEachSubfield(data, *field, [&](int id, const TlvField& f) -> Result<void> {
                if (id == 5) {
                    out.txid = f.value == kNoTxid ? string_view{} : f.value;
                    txidOffset = f.offset + 4;
                    return {};
                }
                return out.addExtra({"62", f.id, f.value});
            });
            if (!sub) {
                return unexpected(sub.error());
            }
            break;
        }
        case 63: {
            if (field->end != data.size()) {
                return fail(ErrorCode::InvalidValue, offset, "campo 63 deve ser o último");
            }
            if (value.size() != 4) {
                return fail(ErrorCode::InvalidLength, offset + 2, "CRC deve ter 4 caracteres");
            }
            const auto expected = crc16Hex(crc16Ccitt(data.substr(0, offset + 4)));
            if (!equalsIgnoreCase(value, string_view(expected.data(), expected.size()))) {
                return fail(ErrorCode::InvalidCheckDigit, offset + 4, "CRC não confere");
            }
            break;
        }
        default:
            if (const auto added = out.addExtra({"", field->id, value}); !added) {
                return rebase(added.error(), offset);
            }
            break;
        }
        offset = field->end;
    }

    for (const int required : {26, 52, 53, 58, 59, 60, 63}) {
        if (!seen[static_cast<size_t>(required)]) {
            return fail(ErrorCode::MissingField, data.size(), "campo obrigatório ausente");
        }
    }
    if (const auto account = validateAccount(out, keyOffset, urlOffset); !account) {
        return unexpected(account.error());
    }
    if (out.amount) {
        if (const auto amount = validateAmount(*out.amount); !amount) {
            return rebase(amount.error(), amountOffset);
        }
    }
    if (const auto name = validateText(out.merchantName, kMaxName, "nome vazio", "nome acima de 25 caracteres"); !name) {
        return rebase(name.error(), offsets[59]);
    }
    if (const auto city = validateText(out.merchantCity, kMaxCity, "cidade vazia", "cidade acima de 15 caracteres"); !city) {
        return rebase(city.error(), offsets[60]);
    }
    if (!out.isDynamic()) {
        if (const auto txid = validateTxid(out.txid); !txid) {
            return rebase(txid.error(), txidOffset);
        }
    }
    return out;
}

Result<void> parseBatch(span<const string_view> inputs, span<Result<Payload>> outputs) noexcept {
    if (outputs.size() < inputs.size()) {
        return fail(ErrorCode::InvalidLength, 0, "saída menor que a entrada");
    }
    for (size_t i = 0; i < inputs.size(); ++i) {
        outputs[i] = parse(inputs[i]);
    }
    return {};
}

Result<string> removeAccents(string_view text) {
    string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        const auto byte = static_cast<unsigned char>(text[i]);
        if (byte < 0x80) {
            out.push_back(text[i]);
            continue;
        }
        if (byte == 0xC3 && i + 1 < text.size()) {
            const auto next = static_cast<unsigned char>(text[i + 1]);
            if (next >= 0x80 && next <= 0xBF && kAccentMap[next - 0x80] != '-') {
                out.push_back(kAccentMap[next - 0x80]);
                ++i;
                continue;
            }
        }
        return fail(ErrorCode::InvalidCharacter, i, "caractere sem equivalente ASCII");
    }
    return out;
}

Result<string> build(const Payload& p) {
    if (const auto account = validateAccount(p, 0, 0); !account) {
        return unexpected(account.error());
    }
    if (p.amount) {
        if (const auto amount = validateAmount(*p.amount); !amount) {
            return unexpected(amount.error());
        }
    }
    const auto name = removeAccents(p.merchantName);
    if (!name) {
        return unexpected(name.error());
    }
    if (const auto valid = validateText(*name, kMaxName, "nome vazio", "nome acima de 25 caracteres"); !valid) {
        return unexpected(valid.error());
    }
    const auto city = removeAccents(p.merchantCity);
    if (!city) {
        return unexpected(city.error());
    }
    if (const auto valid = validateText(*city, kMaxCity, "cidade vazia", "cidade acima de 15 caracteres"); !valid) {
        return unexpected(valid.error());
    }
    if (const auto mcc = validateDigits(p.merchantCategory, 4, "MCC deve ter 4 dígitos"); !mcc) {
        return unexpected(mcc.error());
    }
    if (!p.postalCode.empty()) {
        if (const auto cep = validateDigits(p.postalCode, 8, "CEP deve ter 8 dígitos"); !cep) {
            return unexpected(cep.error());
        }
    }
    const string_view txid = p.txid.empty() ? kNoTxid : p.txid;
    if (!p.isDynamic()) {
        if (const auto valid = validateTxid(txid); !valid) {
            return unexpected(valid.error());
        }
    }

    vector<Entry> account{{"00", string(kGui)}};
    if (p.isDynamic()) {
        account.push_back({"25", string(p.url)});
    } else {
        account.push_back({"01", string(p.key)});
    }
    if (!p.description.empty()) {
        account.push_back({"02", string(p.description)});
    }
    vector<Entry> additional{{"05", string(txid)}};
    vector<Entry> top;

    for (const RawField& extra : p.extras()) {
        if (extra.parent == "26") {
            account.push_back({extra.id, string(extra.value)});
        } else if (extra.parent == "62") {
            additional.push_back({extra.id, string(extra.value)});
        } else if (extra.parent.empty()) {
            top.push_back({extra.id, string(extra.value)});
        } else {
            return fail(ErrorCode::InvalidValue, 0, "campo extra com parent desconhecido");
        }
    }

    string accountValue;
    if (const auto appended = appendSorted(accountValue, account); !appended) {
        return unexpected(appended.error());
    }
    string additionalValue;
    if (const auto appended = appendSorted(additionalValue, additional); !appended) {
        return unexpected(appended.error());
    }

    top.push_back({"00", "01"});
    if (p.initiation != Initiation::Unspecified) {
        top.push_back({"01", p.initiation == Initiation::Reusable ? "11" : "12"});
    }
    top.push_back({"26", std::move(accountValue)});
    top.push_back({"52", string(p.merchantCategory)});
    top.push_back({"53", "986"});
    if (p.amount) {
        string amount = formatAmount(*p.amount);
        if (amount.size() > kMaxAmount) {
            return fail(ErrorCode::ValueTooLong, kMaxAmount, "valor acima de 13 caracteres");
        }
        top.push_back({"54", std::move(amount)});
    }
    top.push_back({"58", "BR"});
    top.push_back({"59", *name});
    top.push_back({"60", *city});
    if (!p.postalCode.empty()) {
        top.push_back({"61", string(p.postalCode)});
    }
    top.push_back({"62", std::move(additionalValue)});
    for (const Entry& entry : top) {
        if (entry.id == "63") {
            return fail(ErrorCode::InvalidValue, 0, "campo 63 é gerado pelo build");
        }
    }

    string out;
    if (const auto appended = appendSorted(out, top); !appended) {
        return unexpected(appended.error());
    }
    out += "6304";
    const auto crc = crc16Hex(crc16Ccitt(out));
    out.append(crc.data(), crc.size());
    return out;
}

}
