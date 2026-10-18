#include <celerpay/checkdigit.hpp>
#include <celerpay/crc16.hpp>
#include <celerpay/tlv.hpp>

#include <catch2/catch_test_macros.hpp>

#include <random>
#include <string>

using namespace std;
using namespace celerpay;

TEST_CASE("mod10 com campos da linha digitavel", "[checkdigit]") {
    CHECK(*mod10("001905009") == 5);
    CHECK(*mod10("4014481606") == 9);
    CHECK(*mod10("0680935031") == 4);
    CHECK(*mod10("0") == 0);
    STATIC_REQUIRE(*mod10("123456") == 6);
}

TEST_CASE("mod10 recusa entrada invalida", "[checkdigit]") {
    CHECK(mod10("").error().code == ErrorCode::EmptyInput);
    const auto bad = mod10("12a4");
    CHECK(bad.error().code == ErrorCode::InvalidCharacter);
    CHECK(bad.error().offset == 2);
}

TEST_CASE("mod11Remainder", "[checkdigit]") {
    CHECK(*mod11Remainder("529982247", 10) == 9);
    CHECK(*mod11Remainder("5299822472", 11) == 6);
    CHECK(*mod11Remainder("112223330001") == 3);
    CHECK(mod11Remainder("").error().code == ErrorCode::EmptyInput);
    CHECK(mod11Remainder("1x").error().offset == 1);
}

TEST_CASE("crc16Ccitt vetor padrao", "[crc16]") {
    STATIC_REQUIRE(crc16Ccitt("123456789") == 0x29B1);
    STATIC_REQUIRE(crc16Ccitt("") == 0xFFFF);
    STATIC_REQUIRE(crc16Hex(0x29B1) == array<char, 4>{'2', '9', 'B', '1'});
    STATIC_REQUIRE(crc16Hex(0x000A) == array<char, 4>{'0', '0', '0', 'A'});
}

TEST_CASE("crc16Ccitt do exemplo do manual do BR Code", "[crc16]") {
    const string_view payload = "00020126580014br.gov.bcb.pix0136123e4567-e12b-12d1-a456-426655440000"
                                "5204000053039865802BR5913Fulano de Tal6008BRASILIA62070503***6304";
    CHECK(crc16Ccitt(payload) == 0x1D3D);
}

TEST_CASE("crc16Ccitt por tabela igual a referencia", "[crc16]") {
    mt19937 rng(12345);
    uniform_int_distribution<int> byte(0, 255);
    for (size_t length = 0; length < 300; ++length) {
        string data(length, '\0');
        for (char& c : data) {
            c = static_cast<char>(byte(rng));
        }
        REQUIRE(crc16Ccitt(data) == detail::crc16Reference(data));
    }
}

TEST_CASE("readTlv le campos em sequencia", "[tlv]") {
    const string_view data = "000201010212";
    const auto first = readTlv(data, 0);
    REQUIRE(first);
    CHECK(first->id == "00");
    CHECK(first->value == "01");
    CHECK(first->offset == 0);
    CHECK(first->end == 6);

    const auto second = readTlv(data, first->end);
    REQUIRE(second);
    CHECK(second->id == "01");
    CHECK(second->value == "12");
    CHECK(second->end == data.size());

    STATIC_REQUIRE(readTlv("0500", 0)->value.empty());
}

TEST_CASE("readTlv recusa campos quebrados", "[tlv]") {
    auto check = [](string_view data, ErrorCode code, uint64_t offset) {
        const auto result = readTlv(data, 0);
        INFO(data);
        REQUIRE_FALSE(result);
        CHECK(result.error().code == code);
        CHECK(result.error().offset == offset);
    };
    check("", ErrorCode::UnexpectedEnd, 0);
    check("000", ErrorCode::UnexpectedEnd, 3);
    check("0A02xx", ErrorCode::InvalidCharacter, 1);
    check("00x2xx", ErrorCode::InvalidCharacter, 2);
    check("0005abc", ErrorCode::UnexpectedEnd, 7);
}

TEST_CASE("appendTlv", "[tlv]") {
    string out;
    REQUIRE(appendTlv(out, "00", "01"));
    REQUIRE(appendTlv(out, "59", "Fulano de Tal"));
    REQUIRE(appendTlv(out, "05", ""));
    CHECK(out == "0002015913Fulano de Tal0500");

    CHECK(appendTlv(out, "1", "x").error().code == ErrorCode::InvalidLength);
    CHECK(appendTlv(out, "1x", "x").error().code == ErrorCode::InvalidCharacter);
    CHECK(appendTlv(out, "01", string(100, 'a')).error().code == ErrorCode::ValueTooLong);
    REQUIRE(appendTlv(out, "01", string(99, 'a')));
}

TEST_CASE("appendTlv e readTlv fazem ida e volta", "[tlv]") {
    string out;
    for (int i = 0; i < 100; ++i) {
        const string id = to_string(10 + i % 90);
        REQUIRE(appendTlv(out, id, string(static_cast<size_t>(i), 'v')));
    }
    size_t offset = 0;
    for (int i = 0; i < 100; ++i) {
        const auto field = readTlv(out, offset);
        REQUIRE(field);
        CHECK(field->id == to_string(10 + i % 90));
        CHECK(field->value.size() == static_cast<size_t>(i));
        offset = field->end;
    }
    CHECK(offset == out.size());
}
