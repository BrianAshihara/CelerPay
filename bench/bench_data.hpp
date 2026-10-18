#pragma once

#include <celerpay/boleto.hpp>
#include <celerpay/document.hpp>

#include <cstdlib>
#include <fstream>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace bench {

inline std::vector<std::string> loadOrGenerate(std::string_view name, std::vector<std::string> (*generate)()) {
    if (const char* dir = std::getenv("CELERPAY_BENCH_DATA")) {
        std::ifstream file(std::string(dir) + "/" + std::string(name) + ".txt");
        std::vector<std::string> lines;
        for (std::string line; std::getline(file, line);) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            lines.push_back(line);
        }
        if (!lines.empty()) {
            return lines;
        }
    }
    return generate();
}

inline std::vector<std::string> generateCpfs() {
    std::mt19937 rng(1);
    std::uniform_int_distribution<int> base(100000000, 999999999);
    std::vector<std::string> out;
    while (out.size() < 100000) {
        if (const auto cpf = celerpay::makeCpf(std::to_string(base(rng)))) {
            out.emplace_back(cpf->digits());
        }
    }
    return out;
}

inline std::vector<std::string> generateCnpjs(std::string_view alphabet) {
    std::mt19937 rng(2);
    std::uniform_int_distribution<size_t> pick(0, alphabet.size() - 1);
    std::vector<std::string> out;
    while (out.size() < 100000) {
        std::string base(12, '0');
        for (char& c : base) {
            c = alphabet[pick(rng)];
        }
        if (const auto cnpj = celerpay::makeCnpj(base)) {
            out.emplace_back(cnpj->chars());
        }
    }
    return out;
}

inline std::vector<std::string> generateNumericCnpjs() { return generateCnpjs("0123456789"); }

inline std::vector<std::string> generateAlphanumericCnpjs() { return generateCnpjs("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"); }

inline std::vector<std::string> generateBoletos() {
    std::mt19937 rng(3);
    std::uniform_int_distribution<int> digit(0, 9);
    std::vector<std::string> out;
    while (out.size() < 100000) {
        std::string free(25, '0');
        for (char& c : free) {
            c = static_cast<char>('0' + digit(rng));
        }
        celerpay::boleto::BankFields fields;
        fields.bank = "341";
        fields.dueFactor = static_cast<std::uint16_t>(1000 + out.size() % 9000);
        fields.amount = celerpay::Money::fromCents(static_cast<std::int64_t>(out.size()) + 1);
        fields.freeField = free;
        if (const auto barcode = celerpay::boleto::makeBankBarcode(fields)) {
            out.push_back(celerpay::boleto::formatLine(*barcode, false));
        }
    }
    return out;
}

inline const std::vector<std::string>& cpfs() {
    static const auto data = loadOrGenerate("cpf", generateCpfs);
    return data;
}

inline const std::vector<std::string>& cnpjs() {
    static const auto data = loadOrGenerate("cnpj", generateNumericCnpjs);
    return data;
}

inline const std::vector<std::string>& alphanumericCnpjs() {
    static const auto data = loadOrGenerate("cnpj_alnum", generateAlphanumericCnpjs);
    return data;
}

inline const std::vector<std::string>& boletos() {
    static const auto data = loadOrGenerate("boleto", generateBoletos);
    return data;
}

inline std::vector<std::string_view> views(const std::vector<std::string>& data) { return {data.begin(), data.end()}; }

}
