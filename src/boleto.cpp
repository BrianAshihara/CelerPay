#include <celerpay/boleto.hpp>

#include <celerpay/checkdigit.hpp>

#include "simd/kernels.hpp"

#include <algorithm>

using namespace std;
using namespace std::chrono;

namespace celerpay::boleto {

namespace {

using celerpay::detail::fail;
using celerpay::detail::isDigit;

using Digits = array<char, 44>;

constexpr sys_days kFactorBase = sys_days{1997y / October / 7d};
constexpr int kCycle = 9000;
constexpr string_view kBankMask = "#####.##### #####.###### #####.###### # ##############";
constexpr string_view kCollectionMask = "###########-# ###########-# ###########-# ###########-#";
constexpr size_t kBankLine = 47;
constexpr size_t kCollectionLine = 48;

constexpr array<size_t, 44> kBankLineIndex = [] {
    array<size_t, 44> map{};
    for (size_t i = 0; i < 4; ++i) {
        map[i] = i;
    }
    map[4] = 32;
    for (size_t i = 5; i < 19; ++i) {
        map[i] = 33 + (i - 5);
    }
    for (size_t i = 19; i < 24; ++i) {
        map[i] = 4 + (i - 19);
    }
    for (size_t i = 24; i < 34; ++i) {
        map[i] = 10 + (i - 24);
    }
    for (size_t i = 34; i < 44; ++i) {
        map[i] = 21 + (i - 34);
    }
    return map;
}();

constexpr array<size_t, 44> kCollectionLineIndex = [] {
    array<size_t, 44> map{};
    for (size_t i = 0; i < 44; ++i) {
        map[i] = i / 11 * 12 + i % 11;
    }
    return map;
}();

int64_t toNumber(string_view digits) noexcept {
    int64_t value = 0;
    for (const char c : digits) {
        value = value * 10 + (c - '0');
    }
    return value;
}

void writeNumber(Digits& digits, size_t offset, size_t width, int64_t value) noexcept {
    for (size_t i = width; i-- > 0;) {
        digits[offset + i] = static_cast<char>('0' + value % 10);
        value /= 10;
    }
}

string_view view(const Digits& digits, size_t offset, size_t length) noexcept {
    return string_view(digits.data(), digits.size()).substr(offset, length);
}

char mod10Digit(string_view digits) noexcept { return static_cast<char>('0' + *mod10(digits)); }

array<char, 43> withoutPosition(const Digits& digits, size_t skip) noexcept {
    array<char, 43> out{};
    size_t k = 0;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i != skip) {
            out[k++] = digits[i];
        }
    }
    return out;
}

char bankGeneralDigit(const Digits& digits) noexcept {
    const auto body = withoutPosition(digits, 4);
    const int dv = 11 - *mod11Remainder(string_view(body.data(), body.size()));
    return static_cast<char>('0' + (dv == 0 || dv >= 10 ? 1 : dv));
}

char collectionDigit(string_view digits, char valueType) noexcept {
    if (valueType == '6' || valueType == '7') {
        return mod10Digit(digits);
    }
    const int remainder = *mod11Remainder(digits);
    return static_cast<char>('0' + (remainder < 2 ? 0 : 11 - remainder));
}

char collectionGeneralDigit(const Digits& digits) noexcept {
    const auto body = withoutPosition(digits, 3);
    return collectionDigit(string_view(body.data(), body.size()), digits[2]);
}

Result<void> validateDigits(const Digits& digits) noexcept {
    if (digits[0] == '8') {
        if (digits[1] == '0') {
            return fail(ErrorCode::InvalidValue, 1, "segmento de arrecadação deve ser de 1 a 9");
        }
        if (digits[2] < '6') {
            return fail(ErrorCode::InvalidValue, 2, "identificador de valor deve ser 6, 7, 8 ou 9");
        }
        if (digits[3] != collectionGeneralDigit(digits)) {
            return fail(ErrorCode::InvalidCheckDigit, 3, "DV geral da arrecadação não confere");
        }
        return {};
    }
    if (digits[3] != '9' && digits[3] != '0') {
        return fail(ErrorCode::InvalidValue, 3, "moeda deve ser 9 (real) ou 0");
    }
    if (digits[4] != bankGeneralDigit(digits)) {
        return fail(ErrorCode::InvalidCheckDigit, 4, "DV geral do boleto não confere");
    }
    return {};
}

template <size_t N>
struct Extracted {
    array<char, N> digits{};
    array<size_t, N> offsets{};
};

template <size_t N>
Result<Extracted<N>> extract(string_view text, string_view mask) noexcept {
    const bool masked = text.size() == mask.size();
    Extracted<N> out;
    size_t k = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if (masked && mask[i] != '#') {
            if (text[i] != mask[i]) {
                return fail(ErrorCode::InvalidCharacter, i, "separador fora da máscara");
            }
            continue;
        }
        if (!isDigit(text[i])) {
            return fail(ErrorCode::InvalidCharacter, i, "esperado um dígito");
        }
        out.digits[k] = text[i];
        out.offsets[k] = i;
        ++k;
    }
    return out;
}

template <size_t N>
Result<Barcode> finishLine(const Extracted<N>& line, const Digits& digits, const array<size_t, 44>& lineIndex);

}

namespace detail {

struct BarcodeAccess {
    static Barcode make(const Digits& digits) noexcept {
        Barcode out;
        out.digits_ = digits;
        return out;
    }

    static const Digits& digits(const Barcode& barcode) noexcept { return barcode.digits_; }
};

}

namespace {

template <size_t N>
Result<Barcode> finishLine(const Extracted<N>& line, const Digits& digits, const array<size_t, 44>& lineIndex) {
    if (const auto valid = validateDigits(digits); !valid) {
        Error error = valid.error();
        error.offset = line.offsets[lineIndex[error.offset]];
        return unexpected(error);
    }
    return detail::BarcodeAccess::make(digits);
}

Result<Barcode> parseBankLine(string_view text) noexcept {
    const auto line = extract<kBankLine>(text, kBankMask);
    if (!line) {
        return unexpected(line.error());
    }
    const string_view d(line->digits.data(), line->digits.size());
    if (d[0] == '8') {
        return fail(ErrorCode::InvalidValue, line->offsets[0], "linha de 47 dígitos não pode começar com 8");
    }
    constexpr array<pair<size_t, size_t>, 3> fields = {{{0, 9}, {10, 10}, {21, 10}}};
    for (const auto& [start, length] : fields) {
        if (d[start + length] != mod10Digit(d.substr(start, length))) {
            return fail(ErrorCode::InvalidCheckDigit, line->offsets[start + length], "DV de campo da linha digitável não confere");
        }
    }
    Digits digits{};
    for (size_t i = 0; i < digits.size(); ++i) {
        digits[i] = d[kBankLineIndex[i]];
    }
    return finishLine(*line, digits, kBankLineIndex);
}

Result<Barcode> parseCollectionLine(string_view text) noexcept {
    const auto line = extract<kCollectionLine>(text, kCollectionMask);
    if (!line) {
        return unexpected(line.error());
    }
    const string_view d(line->digits.data(), line->digits.size());
    if (d[0] != '8') {
        return fail(ErrorCode::InvalidValue, line->offsets[0], "linha de arrecadação deve começar com 8");
    }
    if (d[2] < '6') {
        return fail(ErrorCode::InvalidValue, line->offsets[2], "identificador de valor deve ser 6, 7, 8 ou 9");
    }
    for (size_t block = 0; block < 4; ++block) {
        const size_t start = block * 12;
        if (d[start + 11] != collectionDigit(d.substr(start, 11), d[2])) {
            return fail(ErrorCode::InvalidCheckDigit, line->offsets[start + 11], "DV de bloco da linha digitável não confere");
        }
    }
    Digits digits{};
    for (size_t i = 0; i < digits.size(); ++i) {
        digits[i] = d[kCollectionLineIndex[i]];
    }
    return finishLine(*line, digits, kCollectionLineIndex);
}

Result<void> requireDigits(string_view text, size_t length, string_view detail) noexcept {
    if (text.size() != length) {
        return fail(ErrorCode::InvalidLength, 0, detail);
    }
    if (simd::activeKernels().allDigits(text.data(), text.size())) {
        return {};
    }
    for (size_t i = 0; i < text.size(); ++i) {
        if (!isDigit(text[i])) {
            return fail(ErrorCode::InvalidCharacter, i, "esperado um dígito");
        }
    }
    return {};
}

}

Result<Barcode> parseBarcode(string_view text) noexcept {
    if (const auto valid = requireDigits(text, 44, "código de barras deve ter 44 dígitos"); !valid) {
        return unexpected(valid.error());
    }
    Digits digits{};
    copy(text.begin(), text.end(), digits.begin());
    if (const auto valid = validateDigits(digits); !valid) {
        return unexpected(valid.error());
    }
    return detail::BarcodeAccess::make(digits);
}

Result<Barcode> parseLine(string_view text) noexcept {
    switch (text.size()) {
    case kBankLine:
    case kBankMask.size():
        return parseBankLine(text);
    case kCollectionLine:
    case kCollectionMask.size():
        return parseCollectionLine(text);
    default:
        return fail(ErrorCode::InvalidLength, 0, "linha digitável deve ter 47 ou 48 dígitos");
    }
}

Result<void> parseBatch(span<const string_view> inputs, span<Result<Barcode>> outputs) noexcept {
    if (outputs.size() < inputs.size()) {
        return fail(ErrorCode::InvalidLength, 0, "saída menor que a entrada");
    }
    for (size_t i = 0; i < inputs.size(); ++i) {
        outputs[i] = parse(inputs[i]);
    }
    return {};
}

Result<Barcode> parse(string_view text) noexcept {
    if (text.size() == 44) {
        return parseBarcode(text);
    }
    return parseLine(text);
}

string formatLine(const Barcode& barcode, bool masked) {
    const Digits& digits = detail::BarcodeAccess::digits(barcode);
    const bool bank = barcode.kind() == Kind::Bank;
    const auto& lineIndex = bank ? kBankLineIndex : kCollectionLineIndex;
    string line(bank ? kBankLine : kCollectionLine, '0');
    for (size_t i = 0; i < digits.size(); ++i) {
        line[lineIndex[i]] = digits[i];
    }
    if (bank) {
        line[9] = mod10Digit(string_view(line).substr(0, 9));
        line[20] = mod10Digit(string_view(line).substr(10, 10));
        line[31] = mod10Digit(string_view(line).substr(21, 10));
    } else {
        for (size_t block = 0; block < 4; ++block) {
            line[block * 12 + 11] = collectionDigit(string_view(line).substr(block * 12, 11), digits[2]);
        }
    }
    if (!masked) {
        return line;
    }
    string out(bank ? kBankMask : kCollectionMask);
    size_t k = 0;
    for (char& c : out) {
        if (c == '#') {
            c = line[k++];
        }
    }
    return out;
}

BankFields bankFields(const Barcode& barcode) noexcept {
    const Digits& d = detail::BarcodeAccess::digits(barcode);
    BankFields out;
    out.bank = view(d, 0, 3);
    out.currency = d[3];
    out.dueFactor = static_cast<uint16_t>(toNumber(view(d, 5, 4)));
    if (const int64_t cents = toNumber(view(d, 9, 10)); cents != 0) {
        out.amount = Money::fromCents(cents);
    }
    out.freeField = view(d, 19, 25);
    return out;
}

CollectionFields collectionFields(const Barcode& barcode) noexcept {
    const Digits& d = detail::BarcodeAccess::digits(barcode);
    CollectionFields out;
    out.segment = d[1];
    out.valueType = d[2];
    out.value = toNumber(view(d, 4, 11));
    out.companyAndFree = view(d, 15, 29);
    return out;
}

Result<Barcode> makeBankBarcode(const BankFields& fields) noexcept {
    if (const auto valid = requireDigits(fields.bank, 3, "banco deve ter 3 dígitos"); !valid) {
        return unexpected(valid.error());
    }
    if (fields.bank[0] == '8') {
        return fail(ErrorCode::InvalidValue, 0, "código de banco não pode começar com 8");
    }
    if (fields.currency != '9' && fields.currency != '0') {
        return fail(ErrorCode::InvalidValue, 0, "moeda deve ser 9 (real) ou 0");
    }
    if (fields.dueFactor > 9999) {
        return fail(ErrorCode::InvalidValue, 0, "fator de vencimento acima de 9999");
    }
    const int64_t cents = fields.amount ? fields.amount->cents() : 0;
    if (cents < 0 || cents > 9'999'999'999) {
        return fail(ErrorCode::InvalidValue, 0, "valor deve caber em 10 dígitos");
    }
    if (const auto valid = requireDigits(fields.freeField, 25, "campo livre deve ter 25 dígitos"); !valid) {
        return unexpected(valid.error());
    }
    Digits digits{};
    copy(fields.bank.begin(), fields.bank.end(), digits.begin());
    digits[3] = fields.currency;
    writeNumber(digits, 5, 4, fields.dueFactor);
    writeNumber(digits, 9, 10, cents);
    copy(fields.freeField.begin(), fields.freeField.end(), digits.begin() + 19);
    digits[4] = bankGeneralDigit(digits);
    return detail::BarcodeAccess::make(digits);
}

Result<Barcode> makeCollectionBarcode(const CollectionFields& fields) noexcept {
    if (fields.segment < '1' || fields.segment > '9') {
        return fail(ErrorCode::InvalidValue, 0, "segmento de arrecadação deve ser de 1 a 9");
    }
    if (fields.valueType < '6' || fields.valueType > '9') {
        return fail(ErrorCode::InvalidValue, 0, "identificador de valor deve ser 6, 7, 8 ou 9");
    }
    if (fields.value < 0 || fields.value > 99'999'999'999) {
        return fail(ErrorCode::InvalidValue, 0, "valor deve caber em 11 dígitos");
    }
    if (const auto valid = requireDigits(fields.companyAndFree, 29, "empresa e campo livre devem ter 29 dígitos"); !valid) {
        return unexpected(valid.error());
    }
    Digits digits{};
    digits[0] = '8';
    digits[1] = fields.segment;
    digits[2] = fields.valueType;
    writeNumber(digits, 4, 11, fields.value);
    copy(fields.companyAndFree.begin(), fields.companyAndFree.end(), digits.begin() + 15);
    digits[3] = collectionGeneralDigit(digits);
    return detail::BarcodeAccess::make(digits);
}

optional<year_month_day> dueDate(uint16_t factor, year_month_day reference) noexcept {
    if (factor == 0 || factor > 9999 || !reference.ok()) {
        return nullopt;
    }
    const sys_days first = kFactorBase + days{factor};
    if (factor < 1000) {
        return year_month_day{first};
    }
    const auto distance = (sys_days{reference} - first).count();
    const auto half = kCycle / 2;
    auto cycle = (distance >= -half ? distance + half : distance + half - kCycle + 1) / kCycle;
    cycle = max<decltype(cycle)>(cycle, 0);
    return year_month_day{first + days(cycle * kCycle)};
}

Result<uint16_t> dueFactor(year_month_day date) noexcept {
    if (!date.ok()) {
        return fail(ErrorCode::InvalidValue, 0, "data inválida");
    }
    const auto elapsed = (sys_days{date} - kFactorBase).count();
    if (elapsed < 1) {
        return fail(ErrorCode::InvalidValue, 0, "data anterior a 08/10/1997");
    }
    if (elapsed < 1000) {
        return static_cast<uint16_t>(elapsed);
    }
    return static_cast<uint16_t>((elapsed - 1000) % kCycle + 1000);
}

Result<Hybrid> parseHybrid(string_view boleto, string_view pixPayload) noexcept {
    const auto barcode = parse(boleto);
    if (!barcode) {
        return unexpected(barcode.error());
    }
    if (barcode->kind() != Kind::Bank) {
        return fail(ErrorCode::InvalidValue, 0, "boleto híbrido deve ser bancário");
    }
    const auto payload = pix::parse(pixPayload);
    if (!payload) {
        return unexpected(payload.error());
    }
    const auto fields = bankFields(*barcode);
    if (fields.amount && payload->amount && *fields.amount != *payload->amount) {
        return fail(ErrorCode::InvalidValue, 0, "valor do boleto difere do valor do PIX");
    }
    return Hybrid{*barcode, *payload};
}

}
