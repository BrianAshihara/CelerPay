#include <celerpay/crc16.hpp>
#include <celerpay/document.hpp>
#include <celerpay/pix.hpp>

#include <catch2/catch_test_macros.hpp>

#include <random>
#include <string>

using namespace std;
using namespace celerpay;

namespace {

constexpr string_view kManualExample = "00020126580014br.gov.bcb.pix0136123e4567-e12b-12d1-a456-426655440000"
                                       "5204000053039865802BR5913Fulano de Tal6008BRASILIA62070503***63041D3D";

pix::Payload staticPayload() {
    pix::Payload p;
    p.key = "123e4567-e12b-12d1-a456-426655440000";
    p.merchantName = "Fulano de Tal";
    p.merchantCity = "BRASILIA";
    return p;
}

void requireParseError(string_view payload, ErrorCode code) {
    const auto result = pix::parse(payload);
    INFO(payload);
    REQUIRE_FALSE(result);
    CHECK(result.error().code == code);
}

string withCrc(string body) {
    body += "6304";
    const auto crc = crc16Hex(crc16Ccitt(body));
    return body.append(crc.data(), crc.size());
}

}

TEST_CASE("parse do exemplo do manual do BR Code", "[pix]") {
    const auto p = pix::parse(kManualExample);
    REQUIRE(p);
    CHECK(p->key == "123e4567-e12b-12d1-a456-426655440000");
    CHECK(p->merchantName == "Fulano de Tal");
    CHECK(p->merchantCity == "BRASILIA");
    CHECK(p->merchantCategory == "0000");
    CHECK(p->txid.empty());
    CHECK_FALSE(p->amount);
    CHECK_FALSE(p->isDynamic());
    CHECK(p->initiation == pix::Initiation::Unspecified);
    CHECK(p->extras().empty());
}

TEST_CASE("build reproduz o exemplo do manual", "[pix]") {
    CHECK(*pix::build(staticPayload()) == kManualExample);
    CHECK(*pix::build(*pix::parse(kManualExample)) == kManualExample);
}

TEST_CASE("build com todos os campos", "[pix]") {
    pix::Payload p = staticPayload();
    p.initiation = pix::Initiation::SingleUse;
    p.description = "Pedido 42";
    p.amount = Money::fromCents(12345);
    p.postalCode = "70000000";
    p.txid = "PEDIDO42";
    p.merchantCategory = "5411";
    const auto built = pix::build(p);
    REQUIRE(built);
    CHECK(built->starts_with("000201010212"));
    CHECK(built->find("5406123.45") != string::npos);
    CHECK(built->find("62120508PEDIDO42") != string::npos);
    CHECK(pix::parse(*built) == p);
}

TEST_CASE("build remove acentos de nome e cidade", "[pix]") {
    pix::Payload p = staticPayload();
    p.merchantName = "João Conceição";
    p.merchantCity = "São José";
    const auto built = pix::build(p);
    REQUIRE(built);
    const auto parsed = pix::parse(*built);
    REQUIRE(parsed);
    CHECK(parsed->merchantName == "Joao Conceicao");
    CHECK(parsed->merchantCity == "Sao Jose");
}

TEST_CASE("build recusa campos invalidos", "[pix]") {
    auto expectError = [](pix::Payload p, ErrorCode code) {
        const auto built = pix::build(p);
        REQUIRE_FALSE(built);
        CHECK(built.error().code == code);
    };
    pix::Payload p = staticPayload();
    p.merchantName = "Nome Muito Comprido Demais Para PIX";
    expectError(p, ErrorCode::ValueTooLong);

    p = staticPayload();
    p.merchantCity = "Cidade Comprida Demais";
    expectError(p, ErrorCode::ValueTooLong);

    p = staticPayload();
    p.merchantName = "";
    expectError(p, ErrorCode::MissingField);

    p = staticPayload();
    p.url = "pix.example.com/qr/1";
    expectError(p, ErrorCode::InvalidValue);

    p = staticPayload();
    p.key = "";
    expectError(p, ErrorCode::InvalidValue);

    p = staticPayload();
    p.amount = Money::fromCents(0);
    expectError(p, ErrorCode::InvalidValue);

    p = staticPayload();
    p.amount = Money::fromCents(100'000'000'000'000);
    expectError(p, ErrorCode::ValueTooLong);

    p = staticPayload();
    p.txid = "com-hifen";
    expectError(p, ErrorCode::InvalidCharacter);

    p = staticPayload();
    p.merchantName = "Ærø";
    expectError(p, ErrorCode::InvalidCharacter);

    p = staticPayload();
    p.description = string_view("descricao muito longa que nao cabe junto com a chave aleatoria no campo 26");
    expectError(p, ErrorCode::ValueTooLong);

    p = staticPayload();
    REQUIRE(p.addExtra({"", "59", "duplicado"}));
    expectError(p, ErrorCode::InvalidValue);
}

TEST_CASE("PIX dinamico", "[pix]") {
    pix::Payload p;
    p.initiation = pix::Initiation::SingleUse;
    p.url = "pix.example.com/qr/v2/9d36b84fc70b478fb95c12729b90ca25";
    p.merchantName = "Loja Exemplo";
    p.merchantCity = "SAO PAULO";
    const auto built = pix::build(p);
    REQUIRE(built);
    const auto parsed = pix::parse(*built);
    REQUIRE(parsed);
    CHECK(parsed->isDynamic());
    CHECK(parsed->url == p.url);
    CHECK(*parsed == p);

    p.url = "https://pix.example.com/qr/1";
    CHECK(pix::build(p).error().code == ErrorCode::InvalidValue);
    p.url = "semhost/qr";
    CHECK(pix::build(p).error().code == ErrorCode::InvalidValue);
    p.url = "pix.example.com";
    CHECK(pix::build(p).error().code == ErrorCode::InvalidValue);
}

TEST_CASE("parse confere o CRC", "[pix]") {
    string lower(kManualExample);
    lower.replace(lower.size() - 4, 4, "1d3d");
    CHECK(pix::parse(lower));

    string wrong(kManualExample);
    wrong.replace(wrong.size() - 4, 4, "1D3E");
    const auto result = pix::parse(wrong);
    REQUIRE_FALSE(result);
    CHECK(result.error().code == ErrorCode::InvalidCheckDigit);
    CHECK(result.error().offset == wrong.size() - 4);
}

TEST_CASE("parse recusa payloads malformados", "[pix]") {
    requireParseError("", ErrorCode::EmptyInput);
    requireParseError("0102110002016304ABCD", ErrorCode::MissingField);
    requireParseError(withCrc("000202"), ErrorCode::InvalidValue);
    requireParseError(withCrc("000201"), ErrorCode::MissingField);
    requireParseError(withCrc("0002010002015204000053039865802BR5913Fulano de Tal6008BRASILIA"), ErrorCode::InvalidValue);
    requireParseError(withCrc("00020126180014br.gov.bcb.pix5204000053039865802BR5901X6001Y"), ErrorCode::InvalidValue);
    requireParseError(withCrc("00020126580014br.gov.bcb.pix0136123e4567-e12b-12d1-a456-4266554400005204000053038405802BR5901X6001Y"),
                      ErrorCode::InvalidValue);
    requireParseError(withCrc("00020126580014br.gov.bcb.pix0136123e4567-e12b-12d1-a456-4266554400005204000053039865802BR6001Y"),
                      ErrorCode::MissingField);
    requireParseError(string(kManualExample.substr(0, kManualExample.size() - 8)) + "6304", ErrorCode::UnexpectedEnd);
    requireParseError(string(kManualExample) + "0000", ErrorCode::InvalidValue);
}

TEST_CASE("parse aceita GUI em maiusculas e guarda campos desconhecidos", "[pix]") {
    const string payload = withCrc("00020126620014BR.GOV.BCB.PIX0136123e4567-e12b-12d1-a456-4266554400000300"
                                   "27120006other.br5204000053039865802BR5901X6001Y62140503abc5003000800400ab");
    const auto p = pix::parse(payload);
    REQUIRE(p);
    REQUIRE(p->extras().size() == 4);
    CHECK(p->extras()[0] == pix::RawField{"26", "03", ""});
    CHECK(p->extras()[1] == pix::RawField{"", "27", "0006other.br"});
    CHECK(p->extras()[2] == pix::RawField{"62", "50", "000"});
    CHECK(p->extras()[3] == pix::RawField{"", "80", "00ab"});
    CHECK(p->txid == "abc");
    CHECK(pix::parse(*pix::build(*p)) == p);
}

TEST_CASE("validateKey", "[pix][key]") {
    using pix::KeyType;
    CHECK(*pix::validateKey("52998224725") == KeyType::Cpf);
    CHECK(*pix::validateKey("11222333000181") == KeyType::Cnpj);
    CHECK(*pix::validateKey("12ABC34501DE35") == KeyType::Cnpj);
    CHECK(*pix::validateKey("fulano@example.com") == KeyType::Email);
    CHECK(*pix::validateKey("fulano.de+tal@mail.example.com.br") == KeyType::Email);
    CHECK(*pix::validateKey("+5561912345678") == KeyType::Phone);
    CHECK(*pix::validateKey("+556112345678") == KeyType::Phone);
    CHECK(*pix::validateKey("123e4567-e12b-12d1-a456-426655440000") == KeyType::Random);

    CHECK(pix::validateKey("").error().code == ErrorCode::EmptyInput);
    CHECK(pix::validateKey("52998224726").error().code == ErrorCode::InvalidCheckDigit);
    CHECK(pix::validateKey("529.982.247-25").error().code == ErrorCode::InvalidCharacter);
    CHECK(pix::validateKey("fulano@example").error().code == ErrorCode::InvalidValue);
    CHECK(pix::validateKey("@example.com").error().code == ErrorCode::InvalidValue);
    CHECK(pix::validateKey("a@b@example.com").error().code == ErrorCode::InvalidValue);
    CHECK(pix::validateKey("fulano@exa mple.com").error().code == ErrorCode::InvalidCharacter);
    CHECK(pix::validateKey("fulano@-example.com").error().code == ErrorCode::InvalidValue);
    CHECK(pix::validateKey("fulano@example..com").error().code == ErrorCode::InvalidValue);
    CHECK(pix::validateKey(string(70, 'a') + "@example.com").error().code == ErrorCode::ValueTooLong);
    CHECK(pix::validateKey("+1555123456").error().code == ErrorCode::InvalidValue);
    CHECK(pix::validateKey("+55611234567").error().code == ErrorCode::InvalidLength);
    CHECK(pix::validateKey("+550112345678").error().code == ErrorCode::InvalidValue);
    CHECK(pix::validateKey("123e4567-e12b-12d1-a456_426655440000").error().code == ErrorCode::InvalidCharacter);
    CHECK(pix::validateKey("123e4567-e12b-12d1-a456-42665544000g").error().code == ErrorCode::InvalidCharacter);
}

TEST_CASE("removeAccents", "[pix]") {
    CHECK(*pix::removeAccents("São José dos Campos") == "Sao Jose dos Campos");
    CHECK(*pix::removeAccents("ÀÁÂÃÄÅÇÈÉÊËÌÍÎÏÑÒÓÔÕÖÙÚÛÜÝ") == "AAAAAACEEEEIIIINOOOOOUUUUY");
    CHECK(*pix::removeAccents("àáâãäåçèéêëìíîïñòóôõöùúûüýÿ") == "aaaaaaceeeeiiiinooooouuuuyy");
    CHECK(pix::removeAccents("Straße").error().offset == 4);
    CHECK(pix::removeAccents("\xC3").error().code == ErrorCode::InvalidCharacter);
    CHECK(pix::removeAccents("€").error().code == ErrorCode::InvalidCharacter);
}

TEST_CASE("propriedade: build depois parse devolve os mesmos dados", "[pix][property]") {
    mt19937 rng(2026);
    auto pick = [&](int low, int high) { return uniform_int_distribution<int>(low, high)(rng); };
    auto text = [&](string_view alphabet, int minLength, int maxLength) {
        string out(static_cast<size_t>(pick(minLength, maxLength)), ' ');
        for (char& c : out) {
            c = alphabet[static_cast<size_t>(pick(0, static_cast<int>(alphabet.size()) - 1))];
        }
        return out;
    };
    const string_view printable = " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";
    const string_view alnum = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    const string_view hex = "0123456789abcdef";
    const string_view digits = "0123456789";

    for (int iteration = 0; iteration < 3000; ++iteration) {
        string key;
        string url;
        switch (pick(0, 5)) {
        case 0:
            key = makeCpf(text(digits, 9, 9)).transform([](const Cpf& c) { return string(c.digits()); }).value_or("52998224725");
            break;
        case 1:
            key = makeCnpj(text(alnum.substr(0, 36), 12, 12)).transform([](const Cnpj& c) { return string(c.chars()); }).value_or("11222333000181");
            break;
        case 2:
            key = text("abcdefghij0123456789._-", 1, 20) + "@" + text("abcdefghij", 1, 10) + ".com.br";
            break;
        case 3:
            key = "+55" + text("123456789", 2, 2) + text(digits, 8, 9);
            break;
        case 4:
            key = text(hex, 8, 8) + "-" + text(hex, 4, 4) + "-" + text(hex, 4, 4) + "-" + text(hex, 4, 4) + "-" + text(hex, 12, 12);
            break;
        default:
            url = "pix." + text("abcdefghij", 1, 10) + ".com/qr/v2/" + text(alnum, 1, 40);
            break;
        }
        const string name = text(printable, 1, 25);
        const string city = text(printable, 1, 15);
        const string description = url.empty() ? text(printable, 0, 20) : string();
        const string txid = text(alnum, 0, 25);
        const string postal = pick(0, 1) == 1 ? text(digits, 8, 8) : string();
        const string mcc = text(digits, 4, 4);
        const string extraValue = text(printable, 0, 30);

        pix::Payload p;
        p.initiation = static_cast<pix::Initiation>(pick(0, 2));
        p.key = key;
        p.url = url;
        p.description = description;
        p.merchantName = name;
        p.merchantCity = city;
        p.txid = txid;
        p.postalCode = postal;
        p.merchantCategory = mcc;
        if (pick(0, 1) == 1) {
            p.amount = Money::fromCents(pick(1, 1'000'000'000));
        }
        if (pick(0, 1) == 1) {
            REQUIRE(p.addExtra({"", "80", extraValue}));
        }
        if (pick(0, 1) == 1) {
            REQUIRE(p.addExtra({"62", "50", extraValue}));
        }

        const auto built = pix::build(p);
        INFO("key=" << key << " url=" << url << " name=" << name);
        REQUIRE(built);
        const auto parsed = pix::parse(*built);
        INFO(*built);
        REQUIRE(parsed);
        REQUIRE(*parsed == p);
    }
}
