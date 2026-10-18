#include <celerpay/document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace std;
using namespace celerpay;

namespace {

template <class Parse>
void requireError(Parse parse, string_view text, ErrorCode code, uint64_t offset) {
    const auto result = parse(text);
    INFO("entrada: \"" << text << "\"");
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == code);
    CHECK(result.error().offset == offset);
}

}

TEST_CASE("parseCpf aceita digitos e mascara", "[cpf]") {
    const auto raw = parseCpf("52998224725");
    const auto masked = parseCpf("529.982.247-25");
    REQUIRE(raw);
    REQUIRE(masked);
    CHECK(raw->digits() == "52998224725");
    CHECK(*raw == *masked);
    CHECK(formatCpf(*raw) == "529.982.247-25");
    CHECK(isValidCpf("11144477735"));
    CHECK_FALSE(isValidCpf("11144477736"));
}

TEST_CASE("parseCpf recusa entradas invalidas", "[cpf]") {
    requireError(parseCpf, "", ErrorCode::InvalidLength, 0);
    requireError(parseCpf, "5299822472", ErrorCode::InvalidLength, 0);
    requireError(parseCpf, "529.982.24725", ErrorCode::InvalidLength, 0);
    requireError(parseCpf, "529-982.247-25", ErrorCode::InvalidCharacter, 3);
    requireError(parseCpf, "529.982.247.25", ErrorCode::InvalidCharacter, 11);
    requireError(parseCpf, "5299822472a", ErrorCode::InvalidCharacter, 10);
    requireError(parseCpf, "52998224735", ErrorCode::InvalidCheckDigit, 9);
    requireError(parseCpf, "529.982.247-26", ErrorCode::InvalidCheckDigit, 13);
    requireError(parseCpf, "11111111111", ErrorCode::RepeatedDigits, 0);
    requireError(parseCpf, "000.000.000-00", ErrorCode::RepeatedDigits, 0);
}

TEST_CASE("makeCpf calcula os digitos verificadores", "[cpf]") {
    CHECK(makeCpf("529982247")->digits() == "52998224725");
    CHECK(makeCpf("111444777")->digits() == "11144477735");
    CHECK(makeCpf("12345678").error().code == ErrorCode::InvalidLength);
    CHECK(makeCpf("12345678x").error().offset == 8);
    CHECK(makeCpf("333333333").error().code == ErrorCode::RepeatedDigits);

    for (int base = 100000000; base < 100000000 + 2000; ++base) {
        const auto cpf = makeCpf(to_string(base));
        REQUIRE(cpf);
        REQUIRE(parseCpf(cpf->digits()) == cpf);
    }
}

TEST_CASE("parseCnpj numerico", "[cnpj]") {
    const auto raw = parseCnpj("11222333000181");
    const auto masked = parseCnpj("11.222.333/0001-81");
    REQUIRE(raw);
    REQUIRE(masked);
    CHECK(*raw == *masked);
    CHECK_FALSE(raw->isAlphanumeric());
    CHECK(formatCnpj(*raw) == "11.222.333/0001-81");
}

TEST_CASE("parseCnpj alfanumerico com o exemplo da Receita", "[cnpj]") {
    const auto cnpj = parseCnpj("12.ABC.345/01DE-35");
    REQUIRE(cnpj);
    CHECK(cnpj->chars() == "12ABC34501DE35");
    CHECK(cnpj->isAlphanumeric());
    CHECK(formatCnpj(*cnpj) == "12.ABC.345/01DE-35");
    CHECK(makeCnpj("12ABC34501DE")->chars() == "12ABC34501DE35");
}

TEST_CASE("parseCnpj normaliza minusculas", "[cnpj]") {
    const auto lower = parseCnpj("12abc34501de35");
    REQUIRE(lower);
    CHECK(lower->chars() == "12ABC34501DE35");
    CHECK(parseCnpj("12.abc.345/01de-35") == lower);
    CHECK(makeCnpj("12abc34501de")->chars() == "12ABC34501DE35");
}

TEST_CASE("parseCnpj recusa entradas invalidas", "[cnpj]") {
    requireError(parseCnpj, "", ErrorCode::InvalidLength, 0);
    requireError(parseCnpj, "1122233300018", ErrorCode::InvalidLength, 0);
    requireError(parseCnpj, "11.222.333.0001-81", ErrorCode::InvalidCharacter, 10);
    requireError(parseCnpj, "11222333000-81", ErrorCode::InvalidCharacter, 11);
    requireError(parseCnpj, "12ABC34501DE3A", ErrorCode::InvalidCharacter, 13);
    requireError(parseCnpj, "12ABC34501DE36", ErrorCode::InvalidCheckDigit, 13);
    requireError(parseCnpj, "12ABC34501DF35", ErrorCode::InvalidCheckDigit, 12);
    requireError(parseCnpj, "11222333000182", ErrorCode::InvalidCheckDigit, 13);
    requireError(parseCnpj, "00000000000000", ErrorCode::RepeatedDigits, 0);
    requireError(parseCnpj, "12ABC3450-DE35", ErrorCode::InvalidCharacter, 9);
}

TEST_CASE("makeCnpj ida e volta", "[cnpj]") {
    const string_view alphabet = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    for (size_t i = 0; i < alphabet.size(); ++i) {
        string base = "AB12CD34EF56";
        base[i % 12] = alphabet[i];
        const auto cnpj = makeCnpj(base);
        REQUIRE(cnpj);
        REQUIRE(parseCnpj(formatCnpj(*cnpj)) == cnpj);
    }
    CHECK(makeCnpj("12ABC34501D").error().code == ErrorCode::InvalidLength);
    CHECK(makeCnpj("12ABC34501D-").error().offset == 11);
}
