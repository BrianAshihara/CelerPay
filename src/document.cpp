#include <celerpay/document.hpp>

#include "simd/kernels.hpp"

#include <algorithm>
#include <cstring>

using namespace std;

namespace celerpay {

namespace {

using detail::fail;
using detail::isDigit;

constexpr string_view kCpfMask = "###.###.###-##";
constexpr string_view kCnpjMask = "##.###.###/####-##";

constexpr bool isUpper(char c) noexcept { return c >= 'A' && c <= 'Z'; }

constexpr char toUpper(char c) noexcept { return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c; }

template <size_t N>
struct Extracted {
    array<char, N> chars{};
    array<size_t, N> offsets{};
};

template <size_t N>
Result<Extracted<N>> extract(string_view text, string_view mask, size_t alphanumericCount, string_view lengthDetail) noexcept {
    const bool masked = text.size() == mask.size();
    if (!masked && text.size() != N) {
        return fail(ErrorCode::InvalidLength, 0, lengthDetail);
    }
    Extracted<N> out;
    size_t k = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if (masked && mask[i] != '#') {
            if (text[i] != mask[i]) {
                return fail(ErrorCode::InvalidCharacter, i, "separador fora da máscara");
            }
            continue;
        }
        const bool letterAllowed = k < alphanumericCount;
        const char c = letterAllowed ? toUpper(text[i]) : text[i];
        if (!isDigit(c) && !(letterAllowed && isUpper(c))) {
            return fail(ErrorCode::InvalidCharacter, i, letterAllowed ? "esperado dígito ou letra" : "esperado um dígito");
        }
        out.chars[k] = c;
        out.offsets[k] = i;
        ++k;
    }
    return out;
}

constexpr char checkDigit(const char* chars, size_t count, int maxWeight) noexcept {
    int sum = 0;
    int weight = 2;
    for (size_t i = count; i-- > 0;) {
        sum = (sum + (chars[i] - '0') * weight) % 11;
        weight = weight == maxWeight ? 2 : weight + 1;
    }
    return static_cast<char>('0' + (sum < 2 ? 0 : 11 - sum));
}

template <size_t N>
bool allSame(const array<char, N>& chars) noexcept {
    return all_of(chars.begin(), chars.end(), [&](char c) { return c == chars[0]; });
}

constexpr int cpfWeight(size_t count) noexcept { return static_cast<int>(count) + 1; }

constexpr int kCnpjWeight = 9;

template <size_t N>
string applyMask(const array<char, N>& chars, string_view mask) {
    string out(mask);
    size_t k = 0;
    for (char& c : out) {
        if (c == '#') {
            c = chars[k++];
        }
    }
    return out;
}

}

namespace detail {

struct DocumentAccess {
    static Cpf cpf(const array<char, 11>& digits) noexcept {
        Cpf out;
        out.digits_ = digits;
        return out;
    }

    static Cnpj cnpj(const array<char, 14>& chars) noexcept {
        Cnpj out;
        out.chars_ = chars;
        return out;
    }

    static const array<char, 11>& digits(const Cpf& cpf) noexcept { return cpf.digits_; }

    static const array<char, 14>& chars(const Cnpj& cnpj) noexcept { return cnpj.chars_; }
};

}

Result<Cpf> parseCpf(string_view text) noexcept {
    const auto extracted = extract<11>(text, kCpfMask, 0, "CPF deve ter 11 dígitos ou o formato 000.000.000-00");
    if (!extracted) {
        return unexpected(extracted.error());
    }
    const auto& [digits, offsets] = *extracted;
    if (allSame(digits)) {
        return fail(ErrorCode::RepeatedDigits, 0, "CPF com todos os dígitos iguais");
    }
    for (size_t i = 9; i < 11; ++i) {
        if (digits[i] != checkDigit(digits.data(), i, cpfWeight(i))) {
            return fail(ErrorCode::InvalidCheckDigit, offsets[i], "dígito verificador do CPF não confere");
        }
    }
    return detail::DocumentAccess::cpf(digits);
}

bool isValidCpf(string_view text) noexcept { return parseCpf(text).has_value(); }

Result<Cpf> makeCpf(string_view base) noexcept {
    if (base.size() != 9) {
        return fail(ErrorCode::InvalidLength, 0, "base do CPF deve ter 9 dígitos");
    }
    array<char, 11> digits{};
    for (size_t i = 0; i < 9; ++i) {
        if (!isDigit(base[i])) {
            return fail(ErrorCode::InvalidCharacter, i, "esperado um dígito");
        }
        digits[i] = base[i];
    }
    for (size_t i = 9; i < 11; ++i) {
        digits[i] = checkDigit(digits.data(), i, cpfWeight(i));
    }
    if (allSame(digits)) {
        return fail(ErrorCode::RepeatedDigits, 0, "CPF com todos os dígitos iguais");
    }
    return detail::DocumentAccess::cpf(digits);
}

string formatCpf(const Cpf& cpf) { return applyMask(detail::DocumentAccess::digits(cpf), kCpfMask); }

bool Cnpj::isAlphanumeric() const noexcept {
    return any_of(chars_.begin(), chars_.begin() + 12, [](char c) { return isUpper(c); });
}

Result<Cnpj> parseCnpj(string_view text) noexcept {
    const auto extracted = extract<14>(text, kCnpjMask, 12, "CNPJ deve ter 14 caracteres ou o formato 00.000.000/0000-00");
    if (!extracted) {
        return unexpected(extracted.error());
    }
    const auto& [chars, offsets] = *extracted;
    if (allSame(chars)) {
        return fail(ErrorCode::RepeatedDigits, 0, "CNPJ com todos os dígitos iguais");
    }
    for (size_t i = 12; i < 14; ++i) {
        if (chars[i] != checkDigit(chars.data(), i, kCnpjWeight)) {
            return fail(ErrorCode::InvalidCheckDigit, offsets[i], "dígito verificador do CNPJ não confere");
        }
    }
    return detail::DocumentAccess::cnpj(chars);
}

bool isValidCnpj(string_view text) noexcept { return parseCnpj(text).has_value(); }

Result<Cnpj> makeCnpj(string_view base) noexcept {
    if (base.size() != 12) {
        return fail(ErrorCode::InvalidLength, 0, "base do CNPJ deve ter 12 caracteres");
    }
    array<char, 14> chars{};
    for (size_t i = 0; i < 12; ++i) {
        const char c = toUpper(base[i]);
        if (!isDigit(c) && !isUpper(c)) {
            return fail(ErrorCode::InvalidCharacter, i, "esperado dígito ou letra");
        }
        chars[i] = c;
    }
    for (size_t i = 12; i < 14; ++i) {
        chars[i] = checkDigit(chars.data(), i, kCnpjWeight);
    }
    if (allSame(chars)) {
        return fail(ErrorCode::RepeatedDigits, 0, "CNPJ com todos os dígitos iguais");
    }
    return detail::DocumentAccess::cnpj(chars);
}

string formatCnpj(const Cnpj& cnpj) { return applyMask(detail::DocumentAccess::chars(cnpj), kCnpjMask); }

namespace {

constexpr size_t kChunk = 64;

constexpr simd::DocumentLayout kCpfLayout = [] {
    simd::DocumentLayout layout;
    for (size_t i = 0; i < 11; ++i) {
        layout.digit[i] = 1;
    }
    for (size_t i = 0; i < 9; ++i) {
        layout.weights1[i] = static_cast<uint8_t>(10 - i);
    }
    for (size_t i = 0; i < 10; ++i) {
        layout.weights2[i] = static_cast<uint8_t>(11 - i);
    }
    return layout;
}();

constexpr simd::DocumentLayout kCnpjLayout = [] {
    simd::DocumentLayout layout;
    constexpr array<uint8_t, 13> weights = {6, 5, 4, 3, 2, 9, 8, 7, 6, 5, 4, 3, 2};
    for (size_t i = 0; i < 12; ++i) {
        layout.alnum[i] = 1;
        layout.weights1[i] = weights[i + 1];
    }
    layout.digit[12] = 1;
    layout.digit[13] = 1;
    for (size_t i = 0; i < 13; ++i) {
        layout.weights2[i] = weights[i];
    }
    return layout;
}();

constexpr char digitFromSum(int32_t sum) noexcept {
    const int remainder = sum % 11;
    return static_cast<char>('0' + (remainder < 2 ? 0 : 11 - remainder));
}

template <size_t N, class Doc, class Make, class Slow>
Result<void> parseDocumentBatch(span<const string_view> inputs, span<Result<Doc>> outputs, const simd::DocumentLayout& layout,
                                Make make, Slow slow) noexcept {
    if (outputs.size() < inputs.size()) {
        return fail(ErrorCode::InvalidLength, 0, "saída menor que a entrada");
    }
    const auto& kernels = simd::activeKernels();
    alignas(32) array<char, kChunk * 16> records{};
    array<simd::DocumentSums, kChunk> sums{};
    for (size_t begin = 0; begin < inputs.size(); begin += kChunk) {
        const size_t count = min(kChunk, inputs.size() - begin);
        for (size_t j = 0; j < count; ++j) {
            char* record = records.data() + j * 16;
            memset(record, 0, 16);
            if (inputs[begin + j].size() == N) {
                memcpy(record, inputs[begin + j].data(), N);
            }
        }
        kernels.documentSums(records.data(), count, layout, sums.data());
        for (size_t j = 0; j < count; ++j) {
            const string_view input = inputs[begin + j];
            const char* record = records.data() + j * 16;
            const bool fast = input.size() == N && sums[j].valid != 0 && record[N - 2] == digitFromSum(sums[j].first) &&
                              record[N - 1] == digitFromSum(sums[j].second) &&
                              any_of(record + 1, record + N, [&](char c) { return c != record[0]; });
            if (fast) {
                array<char, N> chars{};
                memcpy(chars.data(), record, N);
                outputs[begin + j] = make(chars);
            } else {
                outputs[begin + j] = slow(input);
            }
        }
    }
    return {};
}

}

Result<void> parseCpfBatch(span<const string_view> inputs, span<Result<Cpf>> outputs) noexcept {
    return parseDocumentBatch<11>(inputs, outputs, kCpfLayout, detail::DocumentAccess::cpf, parseCpf);
}

Result<void> parseCnpjBatch(span<const string_view> inputs, span<Result<Cnpj>> outputs) noexcept {
    return parseDocumentBatch<14>(inputs, outputs, kCnpjLayout, detail::DocumentAccess::cnpj, parseCnpj);
}

const char* simdBackend() noexcept { return simd::activeKernels().name; }

}
