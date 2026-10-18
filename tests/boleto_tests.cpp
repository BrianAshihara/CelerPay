#include <celerpay/boleto.hpp>
#include <celerpay/checkdigit.hpp>

#include <catch2/catch_test_macros.hpp>

#include <random>
#include <string>

using namespace std;
using namespace std::chrono;
using namespace celerpay;

namespace {

constexpr string_view kBarcode = "00193373700000001000500940144816060680935031";
constexpr string_view kLine = "00190500954014481606906809350314337370000000100";
constexpr string_view kMaskedLine = "00190.50095 40144.816069 06809.350314 3 37370000000100";

template <class Parse>
void requireError(Parse parse, string_view text, ErrorCode code, uint64_t offset) {
    const auto result = parse(text);
    INFO("entrada: \"" << text << "\"");
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == code);
    CHECK(result.error().offset == offset);
}

string digitsOf(mt19937& rng, size_t length) {
    string out(length, '0');
    for (char& c : out) {
        c = static_cast<char>('0' + uniform_int_distribution<int>(0, 9)(rng));
    }
    return out;
}

}

TEST_CASE("exemplo do Banco do Brasil nos dois sentidos", "[boleto]") {
    const auto fromBarcode = boleto::parseBarcode(kBarcode);
    const auto fromLine = boleto::parseLine(kLine);
    const auto fromMasked = boleto::parseLine(kMaskedLine);
    REQUIRE(fromBarcode);
    REQUIRE(fromLine);
    REQUIRE(fromMasked);
    CHECK(*fromBarcode == *fromLine);
    CHECK(*fromBarcode == *fromMasked);
    CHECK(fromBarcode->kind() == boleto::Kind::Bank);
    CHECK(boleto::formatLine(*fromBarcode) == kMaskedLine);
    CHECK(boleto::formatLine(*fromBarcode, false) == kLine);
    CHECK(boleto::parse(kBarcode) == fromBarcode);
    CHECK(boleto::parse(kMaskedLine) == fromBarcode);

    const auto fields = boleto::bankFields(*fromBarcode);
    CHECK(fields.bank == "001");
    CHECK(fields.currency == '9');
    CHECK(fields.dueFactor == 3737);
    CHECK(fields.amount == Money::fromCents(100));
    CHECK(fields.freeField == "0500940144816060680935031");
    CHECK(boleto::dueDate(fields.dueFactor, 2008y / January / 1d) == 2007y / December / 31d);
    CHECK(boleto::makeBankBarcode(fields) == fromBarcode);
}

TEST_CASE("parseBarcode recusa entradas invalidas", "[boleto]") {
    requireError(boleto::parseBarcode, "", ErrorCode::InvalidLength, 0);
    requireError(boleto::parseBarcode, kBarcode.substr(1), ErrorCode::InvalidLength, 0);
    requireError(boleto::parseBarcode, "0019337370000000100050094014481606068093503a", ErrorCode::InvalidCharacter, 43);
    requireError(boleto::parseBarcode, "00194373700000001000500940144816060680935031", ErrorCode::InvalidCheckDigit, 4);
    requireError(boleto::parseBarcode, "00193373700000001000500940144816060680935032", ErrorCode::InvalidCheckDigit, 4);
    requireError(boleto::parseBarcode, "00113373700000001000500940144816060680935031", ErrorCode::InvalidValue, 3);
}

TEST_CASE("parseLine aponta o DV errado", "[boleto]") {
    requireError(boleto::parseLine, "00190500944014481606906809350314337370000000100", ErrorCode::InvalidCheckDigit, 9);
    requireError(boleto::parseLine, "00190500954014481606806809350314337370000000100", ErrorCode::InvalidCheckDigit, 20);
    requireError(boleto::parseLine, "00190500954014481606906809350315337370000000100", ErrorCode::InvalidCheckDigit, 31);
    requireError(boleto::parseLine, "00190500954014481606906809350314437370000000100", ErrorCode::InvalidCheckDigit, 32);
    requireError(boleto::parseLine, "00190.50095 40144.816069 06809.350314 4 37370000000100", ErrorCode::InvalidCheckDigit, 38);
    requireError(boleto::parseLine, "00190.50095 40144.816069 06809.350314 3 37370000000101", ErrorCode::InvalidCheckDigit, 38);
    requireError(boleto::parseLine, "00190-50095 40144.816069 06809.350314 3 37370000000100", ErrorCode::InvalidCharacter, 5);
    requireError(boleto::parseLine, "0019050095401448160690680935031433737000000010", ErrorCode::InvalidLength, 0);
    requireError(boleto::parseLine, "80190500954014481606906809350314337370000000100", ErrorCode::InvalidValue, 0);
}

TEST_CASE("fator de vencimento em volta do reinicio de 2025", "[boleto][factor]") {
    CHECK(boleto::dueDate(1000, 2001y / January / 1d) == 2000y / July / 3d);
    CHECK(boleto::dueDate(9999, 2025y / February / 1d) == 2025y / February / 21d);
    CHECK(boleto::dueDate(9999, 2025y / March / 10d) == 2025y / February / 21d);
    CHECK(boleto::dueDate(1000, 2025y / February / 1d) == 2025y / February / 22d);
    CHECK(boleto::dueDate(1000, 2025y / February / 22d) == 2025y / February / 22d);
    CHECK(boleto::dueDate(1001, 2025y / March / 1d) == 2025y / February / 23d);
    CHECK(boleto::dueDate(9000, 2026y / January / 1d) == year_month_day{sys_days{2000y / July / 3d} + days{8000}});
    CHECK(boleto::dueDate(1500, 2026y / January / 1d) == 2026y / July / 7d);
    CHECK(boleto::dueDate(3737, 2026y / January / 1d) == year_month_day{sys_days{2007y / December / 31d} + days{9000}});
    CHECK(boleto::dueDate(999, 2026y / January / 1d) == 2000y / July / 2d);
    CHECK(boleto::dueDate(1, 2026y / January / 1d) == 1997y / October / 8d);
    CHECK_FALSE(boleto::dueDate(0, 2026y / January / 1d));
    CHECK_FALSE(boleto::dueDate(10000, 2026y / January / 1d));
    CHECK_FALSE(boleto::dueDate(1000, 2026y / February / 30d));

    CHECK(*boleto::dueFactor(2025y / February / 21d) == 9999);
    CHECK(*boleto::dueFactor(2025y / February / 22d) == 1000);
    CHECK(*boleto::dueFactor(2000y / July / 3d) == 1000);
    CHECK(*boleto::dueFactor(2000y / July / 2d) == 999);
    CHECK(*boleto::dueFactor(year_month_day{sys_days{2025y / February / 21d} + days{9000}}) == 9999);
    CHECK(*boleto::dueFactor(year_month_day{sys_days{2025y / February / 22d} + days{9000}}) == 1000);
    CHECK(boleto::dueFactor(1997y / October / 7d).error().code == ErrorCode::InvalidValue);
    CHECK(boleto::dueFactor(2025y / February / 29d).error().code == ErrorCode::InvalidValue);
}

TEST_CASE("fator e data fazem ida e volta perto da referencia", "[boleto][factor]") {
    for (sys_days day = sys_days{2000y / July / 3d}; day < sys_days{2060y / January / 1d}; day += days{7}) {
        const year_month_day date{day};
        const auto factor = boleto::dueFactor(date);
        REQUIRE(factor);
        REQUIRE(boleto::dueDate(*factor, date) == date);
        REQUIRE(boleto::dueDate(*factor, year_month_day{day + days{4000}}) == date);
        REQUIRE(boleto::dueDate(*factor, year_month_day{day - days{4000}}).has_value());
    }
}

TEST_CASE("boleto de arrecadacao", "[boleto][collection]") {
    for (const char valueType : {'6', '7', '8', '9'}) {
        boleto::CollectionFields fields;
        fields.segment = '8';
        fields.valueType = valueType;
        fields.value = 12345;
        fields.companyAndFree = "01234567890123456789012345678";
        const auto barcode = boleto::makeCollectionBarcode(fields);
        REQUIRE(barcode);
        CHECK(barcode->kind() == boleto::Kind::Collection);
        CHECK(barcode->digits().substr(0, 3) == string{'8', '8', valueType});
        CHECK(barcode->digits().substr(4, 11) == "00000012345");

        const string line = boleto::formatLine(*barcode, false);
        REQUIRE(line.size() == 48);
        for (size_t block = 0; block < 4; ++block) {
            const string_view data = string_view(line).substr(block * 12, 11);
            const int expected = valueType <= '7' ? *mod10(data) : [&] {
                const int r = *mod11Remainder(data);
                return r < 2 ? 0 : 11 - r;
            }();
            CHECK(line[block * 12 + 11] == '0' + expected);
        }
        CHECK(boleto::parseLine(line) == barcode);
        CHECK(boleto::parseLine(boleto::formatLine(*barcode)) == barcode);
        CHECK(boleto::parseBarcode(barcode->digits()) == barcode);

        const auto parsed = boleto::collectionFields(*barcode);
        CHECK(parsed.segment == '8');
        CHECK(parsed.valueType == valueType);
        CHECK(parsed.value == 12345);
        CHECK(parsed.companyAndFree == fields.companyAndFree);
    }
}

TEST_CASE("arrecadacao recusa entradas invalidas", "[boleto][collection]") {
    boleto::CollectionFields fields;
    fields.companyAndFree = "01234567890123456789012345678";
    const auto barcode = boleto::makeCollectionBarcode(fields);
    REQUIRE(barcode);
    string line = boleto::formatLine(*barcode);
    line[12] = line[12] == '0' ? '1' : '0';
    requireError(boleto::parseLine, line, ErrorCode::InvalidCheckDigit, 12);

    string digits(barcode->digits());
    digits[3] = digits[3] == '0' ? '1' : '0';
    requireError(boleto::parseBarcode, digits, ErrorCode::InvalidCheckDigit, 3);
    digits = string(barcode->digits());
    digits[2] = '5';
    requireError(boleto::parseBarcode, digits, ErrorCode::InvalidValue, 2);
    digits[1] = '0';
    requireError(boleto::parseBarcode, digits, ErrorCode::InvalidValue, 1);

    fields.segment = '0';
    CHECK(boleto::makeCollectionBarcode(fields).error().code == ErrorCode::InvalidValue);
    fields.segment = '1';
    fields.valueType = '5';
    CHECK(boleto::makeCollectionBarcode(fields).error().code == ErrorCode::InvalidValue);
    fields.valueType = '6';
    fields.value = 100'000'000'000;
    CHECK(boleto::makeCollectionBarcode(fields).error().code == ErrorCode::InvalidValue);
    fields.value = 1;
    fields.companyAndFree = "123";
    CHECK(boleto::makeCollectionBarcode(fields).error().code == ErrorCode::InvalidLength);
}

TEST_CASE("makeBankBarcode valida campos", "[boleto]") {
    boleto::BankFields fields;
    fields.bank = "341";
    fields.dueFactor = 1000;
    fields.freeField = "1234567890123456789012345";
    const auto barcode = boleto::makeBankBarcode(fields);
    REQUIRE(barcode);
    CHECK_FALSE(boleto::bankFields(*barcode).amount);

    fields.bank = "34";
    CHECK(boleto::makeBankBarcode(fields).error().code == ErrorCode::InvalidLength);
    fields.bank = "841";
    CHECK(boleto::makeBankBarcode(fields).error().code == ErrorCode::InvalidValue);
    fields.bank = "341";
    fields.currency = '1';
    CHECK(boleto::makeBankBarcode(fields).error().code == ErrorCode::InvalidValue);
    fields.currency = '9';
    fields.amount = Money::fromCents(10'000'000'000);
    CHECK(boleto::makeBankBarcode(fields).error().code == ErrorCode::InvalidValue);
    fields.amount = Money::fromCents(-1);
    CHECK(boleto::makeBankBarcode(fields).error().code == ErrorCode::InvalidValue);
    fields.amount = nullopt;
    fields.dueFactor = 10000;
    CHECK(boleto::makeBankBarcode(fields).error().code == ErrorCode::InvalidValue);
}

TEST_CASE("propriedade: barcode, linha e campos fazem ida e volta", "[boleto][property]") {
    mt19937 rng(44);
    for (int i = 0; i < 5000; ++i) {
        string bank = digitsOf(rng, 3);
        if (bank[0] == '8') {
            bank[0] = '7';
        }
        const string freeField = digitsOf(rng, 25);
        boleto::BankFields fields;
        fields.bank = bank;
        fields.dueFactor = static_cast<uint16_t>(uniform_int_distribution<int>(0, 9999)(rng));
        if (const int64_t cents = uniform_int_distribution<int64_t>(0, 9'999'999'999)(rng); cents != 0) {
            fields.amount = Money::fromCents(cents);
        }
        fields.freeField = freeField;
        const auto barcode = boleto::makeBankBarcode(fields);
        REQUIRE(barcode);
        REQUIRE(boleto::parseLine(boleto::formatLine(*barcode)) == barcode);
        REQUIRE(boleto::parseLine(boleto::formatLine(*barcode, false)) == barcode);
        const auto back = boleto::bankFields(*barcode);
        REQUIRE(back.bank == fields.bank);
        REQUIRE(back.dueFactor == fields.dueFactor);
        REQUIRE(back.amount == fields.amount);
        REQUIRE(back.freeField == fields.freeField);
    }
}

TEST_CASE("boleto hibrido", "[boleto][hybrid]") {
    pix::Payload p;
    p.url = "pix.example.com/cobv/9d36b84fc70b478fb95c12729b90ca25";
    p.merchantName = "Banco Exemplo";
    p.merchantCity = "BRASILIA";
    const auto qr = pix::build(p);
    REQUIRE(qr);
    const auto hybrid = boleto::parseHybrid(kMaskedLine, *qr);
    REQUIRE(hybrid);
    CHECK(hybrid->barcode.digits() == kBarcode);
    CHECK(hybrid->pix.isDynamic());

    p.url = {};
    p.key = "52998224725";
    p.amount = Money::fromCents(100);
    const auto same = pix::build(p);
    REQUIRE(same);
    CHECK(boleto::parseHybrid(kBarcode, *same));

    p.amount = Money::fromCents(200);
    const auto different = pix::build(p);
    REQUIRE(different);
    CHECK(boleto::parseHybrid(kBarcode, *different).error().code == ErrorCode::InvalidValue);

    boleto::CollectionFields collection;
    collection.companyAndFree = "01234567890123456789012345678";
    const auto utility = boleto::makeCollectionBarcode(collection);
    REQUIRE(utility);
    CHECK(boleto::parseHybrid(utility->digits(), *same).error().code == ErrorCode::InvalidValue);
    CHECK(boleto::parseHybrid(kBarcode, "invalido").error().code != ErrorCode::InvalidValue);
}
