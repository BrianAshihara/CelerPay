#include <celerpay/qr.hpp>

#include <catch2/catch_test_macros.hpp>

#include <ZXing/ReadBarcode.h>

#include <random>
#include <string>
#include <vector>

using namespace std;
using namespace celerpay;

namespace {

constexpr array<qr::Ecc, 4> kLevels = {qr::Ecc::Low, qr::Ecc::Medium, qr::Ecc::Quartile, qr::Ecc::High};

string decode(const qr::QrMatrix& matrix) {
    constexpr int scale = 2;
    constexpr int quiet = 4;
    const int width = (matrix.size() + quiet * 2) * scale;
    vector<uint8_t> pixels(static_cast<size_t>(width * width), 255);
    for (int y = 0; y < matrix.size(); ++y) {
        for (int x = 0; x < matrix.size(); ++x) {
            if (!matrix.at(x, y)) {
                continue;
            }
            for (int dy = 0; dy < scale; ++dy) {
                for (int dx = 0; dx < scale; ++dx) {
                    pixels[static_cast<size_t>(((y + quiet) * scale + dy) * width + (x + quiet) * scale + dx)] = 0;
                }
            }
        }
    }
    const ZXing::ImageView image(pixels.data(), width, width, ZXing::ImageFormat::Lum);
    const auto options = ZXing::ReaderOptions().setFormats(ZXing::BarcodeFormat::QRCode).setIsPure(true);
    const auto result = ZXing::ReadBarcode(image, options);
    if (!result.isValid()) {
        return "<falhou>";
    }
    const auto& bytes = result.bytes();
    return string(bytes.begin(), bytes.end());
}

string randomBytes(mt19937& rng, size_t length) {
    uniform_int_distribution<int> byte(0, 255);
    string out(length, '\0');
    for (char& c : out) {
        c = static_cast<char>(byte(rng));
    }
    return out;
}

}

TEST_CASE("capacidade em modo byte", "[qr]") {
    CHECK(qr::capacity(1, qr::Ecc::Low) == 17);
    CHECK(qr::capacity(1, qr::Ecc::Medium) == 14);
    CHECK(qr::capacity(1, qr::Ecc::Quartile) == 11);
    CHECK(qr::capacity(1, qr::Ecc::High) == 7);
    CHECK(qr::capacity(10, qr::Ecc::Medium) == 213);
    CHECK(qr::capacity(40, qr::Ecc::Low) == 2953);
    CHECK(qr::capacity(40, qr::Ecc::High) == 1273);
    CHECK(qr::capacity(0, qr::Ecc::Low) == 0);
    CHECK(qr::capacity(41, qr::Ecc::Low) == 0);
}

TEST_CASE("zxing decodifica todas as versoes e niveis", "[qr][zxing]") {
    mt19937 rng(40);
    for (int version = 1; version <= 40; ++version) {
        for (const qr::Ecc ecc : kLevels) {
            const string data = randomBytes(rng, static_cast<size_t>(qr::capacity(version, ecc)));
            const auto matrix = qr::encode(data, {.ecc = ecc, .minVersion = version, .maxVersion = version});
            INFO("versao " << version << " nivel " << static_cast<int>(ecc));
            REQUIRE(matrix);
            CHECK(matrix->version() == version);
            CHECK(matrix->size() == version * 4 + 17);
            CHECK(matrix->ecc() == ecc);
            REQUIRE(decode(*matrix) == data);
        }
    }
}

TEST_CASE("zxing decodifica as 8 mascaras", "[qr][zxing]") {
    mt19937 rng(8);
    for (const int version : {1, 2, 7, 14, 25, 40}) {
        for (int mask = 0; mask < 8; ++mask) {
            const string data = randomBytes(rng, static_cast<size_t>(qr::capacity(version, qr::Ecc::Medium)) / 2);
            const auto matrix = qr::encode(data, {.minVersion = version, .maxVersion = version, .mask = mask});
            INFO("versao " << version << " mascara " << mask);
            REQUIRE(matrix);
            CHECK(matrix->mask() == mask);
            REQUIRE(decode(*matrix) == data);
        }
    }
}

TEST_CASE("escolhe a menor versao que cabe", "[qr]") {
    CHECK(qr::encode(string(14, 'a'))->version() == 1);
    CHECK(qr::encode(string(15, 'a'))->version() == 2);
    CHECK(qr::encode("", {})->version() == 1);
    CHECK(qr::encode(string(14, 'a'), {.minVersion = 5})->version() == 5);
    const auto automatic = qr::encode("CelerPay");
    REQUIRE(automatic);
    CHECK(automatic->mask() >= 0);
    CHECK(automatic->mask() <= 7);
}

TEST_CASE("encode recusa opcoes invalidas", "[qr]") {
    CHECK(qr::encode(string(2954, 'a'), {.ecc = qr::Ecc::Low}).error().code == ErrorCode::ValueTooLong);
    CHECK(qr::encode(string(15, 'a'), {.maxVersion = 1}).error().code == ErrorCode::ValueTooLong);
    CHECK(qr::encode("a", {.minVersion = 0}).error().code == ErrorCode::InvalidValue);
    CHECK(qr::encode("a", {.maxVersion = 41}).error().code == ErrorCode::InvalidValue);
    CHECK(qr::encode("a", {.minVersion = 5, .maxVersion = 4}).error().code == ErrorCode::InvalidValue);
    CHECK(qr::encode("a", {.mask = 8}).error().code == ErrorCode::InvalidValue);
}

TEST_CASE("encode e deterministico", "[qr]") {
    const auto a = qr::encode("determinismo");
    const auto b = qr::encode("determinismo");
    REQUIRE(a);
    REQUIRE(b);
    for (int y = 0; y < a->size(); ++y) {
        for (int x = 0; x < a->size(); ++x) {
            REQUIRE(a->at(x, y) == b->at(x, y));
        }
    }
}

TEST_CASE("toSvg", "[qr][svg]") {
    const auto matrix = qr::encode("svg");
    REQUIRE(matrix);
    const auto svg = qr::toSvg(*matrix);
    REQUIRE(svg);
    CHECK(svg->starts_with("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 29 29\" width=\"232\" height=\"232\""));
    CHECK(svg->find("<rect width=\"29\" height=\"29\" fill=\"#FFFFFF\"/>") != string::npos);
    CHECK(svg->find("<path fill=\"#000000\" d=\"M4 4h7v1h-7z") != string::npos);
    CHECK(svg->ends_with("\"/></svg>"));

    const auto custom = qr::toSvg(*matrix, {.moduleSize = 2, .quietZone = 0, .dark = "rgb(10, 20, 30)", .light = ""});
    REQUIRE(custom);
    CHECK(custom->find("viewBox=\"0 0 21 21\" width=\"42\"") != string::npos);
    CHECK(custom->find("<rect") == string::npos);
    CHECK(custom->find("fill=\"rgb(10, 20, 30)\"") != string::npos);
    CHECK(custom->find("M0 0h7v1h-7z") != string::npos);

    CHECK(qr::toSvg(*matrix, {.moduleSize = 0}).error().code == ErrorCode::InvalidValue);
    CHECK(qr::toSvg(*matrix, {.quietZone = -1}).error().code == ErrorCode::InvalidValue);
    CHECK(qr::toSvg(*matrix, {.dark = "red\"/><script>"}).error().code == ErrorCode::InvalidCharacter);
}

TEST_CASE("pixToSvg gera QR que decodifica para o payload PIX", "[qr][pix]") {
    pix::Payload p;
    p.key = "123e4567-e12b-12d1-a456-426655440000";
    p.merchantName = "Fulano de Tal";
    p.merchantCity = "BRASILIA";
    const auto payload = pix::build(p);
    REQUIRE(payload);
    const auto matrix = qr::encode(*payload);
    REQUIRE(matrix);
    CHECK(decode(*matrix) == *payload);
    CHECK(pix::parse(decode(*matrix)));
    const auto svg = qr::pixToSvg(p);
    REQUIRE(svg);
    CHECK(*svg == *qr::toSvg(*matrix));

    p.merchantName = "";
    CHECK(qr::pixToSvg(p).error().code == ErrorCode::MissingField);
}
