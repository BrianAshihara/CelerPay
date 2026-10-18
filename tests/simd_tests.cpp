#include <celerpay/boleto.hpp>
#include <celerpay/document.hpp>
#include <celerpay/pix.hpp>

#include "simd/kernels.hpp"

#include <catch2/catch_test_macros.hpp>

#include <random>
#include <string>
#include <vector>

using namespace std;
using namespace celerpay;

namespace {

vector<const simd::Kernels*> available() {
    vector<const simd::Kernels*> out;
    if (const auto* avx2 = simd::avx2Kernels(); avx2 != nullptr && string_view(simd::activeKernels().name) == "avx2") {
        out.push_back(avx2);
    }
    if (const auto* neon = simd::neonKernels()) {
        out.push_back(neon);
    }
    return out;
}

char randomChar(mt19937& rng) {
    constexpr string_view common = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcz./-";
    if (uniform_int_distribution<int>(0, 9)(rng) == 0) {
        return static_cast<char>(uniform_int_distribution<int>(0, 255)(rng));
    }
    return common[static_cast<size_t>(uniform_int_distribution<int>(0, static_cast<int>(common.size()) - 1)(rng))];
}

simd::DocumentLayout randomLayout(mt19937& rng) {
    simd::DocumentLayout layout;
    for (size_t i = 0; i < 16; ++i) {
        const int kind = uniform_int_distribution<int>(0, 2)(rng);
        layout.digit[i] = kind == 1 ? 1 : 0;
        layout.alnum[i] = kind == 2 ? 1 : 0;
        layout.weights1[i] = static_cast<uint8_t>(uniform_int_distribution<int>(0, 13)(rng));
        layout.weights2[i] = static_cast<uint8_t>(uniform_int_distribution<int>(0, 13)(rng));
    }
    return layout;
}

}

TEST_CASE("backend SIMD conhecido", "[simd]") {
    const string_view backend = simdBackend();
    CHECK((backend == "avx2" || backend == "neon" || backend == "scalar"));
    UNSCOPED_INFO("backend: " << backend);
}

TEST_CASE("kernels SIMD iguais ao escalar", "[simd]") {
    mt19937 rng(16);
    for (const simd::Kernels* kernels : available()) {
        INFO(kernels->name);
        for (int round = 0; round < 500; ++round) {
            const auto layout = randomLayout(rng);
            const size_t count = static_cast<size_t>(uniform_int_distribution<int>(0, 33)(rng));
            vector<char> records(count * 16 + 16);
            for (char& c : records) {
                c = randomChar(rng);
            }
            vector<simd::DocumentSums> expected(count);
            vector<simd::DocumentSums> actual(count);
            simd::scalarKernels().documentSums(records.data(), count, layout, expected.data());
            kernels->documentSums(records.data(), count, layout, actual.data());
            for (size_t i = 0; i < count; ++i) {
                REQUIRE(actual[i].valid == expected[i].valid);
                REQUIRE(actual[i].first == expected[i].first);
                REQUIRE(actual[i].second == expected[i].second);
            }
        }
        for (int round = 0; round < 2000; ++round) {
            string data(static_cast<size_t>(uniform_int_distribution<int>(0, 100)(rng)), '0');
            for (char& c : data) {
                c = static_cast<char>('0' + uniform_int_distribution<int>(0, 9)(rng));
            }
            if (!data.empty() && round % 2 == 0) {
                data[static_cast<size_t>(uniform_int_distribution<int>(0, static_cast<int>(data.size()) - 1)(rng))] = randomChar(rng);
            }
            REQUIRE(kernels->allDigits(data.data(), data.size()) == simd::scalarKernels().allDigits(data.data(), data.size()));
        }
    }
}

TEST_CASE("parseCpfBatch igual a parseCpf", "[simd][batch]") {
    mt19937 rng(11);
    vector<string> storage;
    for (int i = 0; i < 3000; ++i) {
        const auto cpf = makeCpf(to_string(100000000 + i * 299993 % 900000000));
        string text = cpf ? string(cpf->digits()) : "52998224725";
        switch (i % 6) {
        case 1:
            text[static_cast<size_t>(i % 11)] = static_cast<char>('0' + (text[static_cast<size_t>(i % 11)] - '0' + 1) % 10);
            break;
        case 2:
            text = formatCpf(*parseCpf(text));
            break;
        case 3:
            text[static_cast<size_t>(i % 11)] = randomChar(rng);
            break;
        case 4:
            text = string(11, static_cast<char>('0' + i % 10));
            break;
        case 5:
            text.pop_back();
            break;
        default:
            break;
        }
        storage.push_back(text);
    }
    const vector<string_view> inputs(storage.begin(), storage.end());
    vector<Result<Cpf>> outputs(inputs.size());
    REQUIRE(parseCpfBatch(inputs, outputs));
    for (size_t i = 0; i < inputs.size(); ++i) {
        INFO(inputs[i]);
        REQUIRE(outputs[i] == parseCpf(inputs[i]));
    }
    vector<Result<Cpf>> small(1);
    CHECK(parseCpfBatch(inputs, small).error().code == ErrorCode::InvalidLength);
}

TEST_CASE("parseCnpjBatch igual a parseCnpj", "[simd][batch]") {
    mt19937 rng(14);
    constexpr string_view alphabet = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    vector<string> storage;
    for (int i = 0; i < 3000; ++i) {
        string base(12, '0');
        for (char& c : base) {
            c = alphabet[static_cast<size_t>(uniform_int_distribution<int>(0, i % 2 == 0 ? 9 : 35)(rng))];
        }
        string text = string(makeCnpj(base)->chars());
        switch (i % 7) {
        case 1:
            text[13] = text[13] == '9' ? '0' : static_cast<char>(text[13] + 1);
            break;
        case 2:
            text = formatCnpj(*parseCnpj(text));
            break;
        case 3:
            text[static_cast<size_t>(i % 14)] = randomChar(rng);
            break;
        case 4:
            for (char& c : text) {
                c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
            }
            break;
        case 5:
            text = "00000000000000";
            break;
        default:
            break;
        }
        storage.push_back(text);
    }
    const vector<string_view> inputs(storage.begin(), storage.end());
    vector<Result<Cnpj>> outputs(inputs.size());
    REQUIRE(parseCnpjBatch(inputs, outputs));
    for (size_t i = 0; i < inputs.size(); ++i) {
        INFO(inputs[i]);
        REQUIRE(outputs[i] == parseCnpj(inputs[i]));
    }
}

TEST_CASE("boleto e PIX em lote", "[batch]") {
    const vector<string_view> boletos = {"00193373700000001000500940144816060680935031",
                                         "00190.50095 40144.816069 06809.350314 3 37370000000100", "123", ""};
    vector<Result<boleto::Barcode>> barcodes(boletos.size());
    REQUIRE(boleto::parseBatch(boletos, barcodes));
    for (size_t i = 0; i < boletos.size(); ++i) {
        CHECK(barcodes[i] == boleto::parse(boletos[i]));
    }

    const vector<string_view> payloads = {
        "00020126580014br.gov.bcb.pix0136123e4567-e12b-12d1-a456-4266554400005204000053039865802BR5913Fulano de Tal6008BRASILIA62070503***63041D3D",
        "invalido"};
    vector<Result<pix::Payload>> parsed(payloads.size());
    REQUIRE(pix::parseBatch(payloads, parsed));
    CHECK(parsed[0].has_value());
    CHECK_FALSE(parsed[1].has_value());
    vector<Result<pix::Payload>> none;
    CHECK(pix::parseBatch(payloads, none).error().code == ErrorCode::InvalidLength);
}
