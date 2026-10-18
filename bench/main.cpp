#include "bench_data.hpp"

#include <celerpay/document.hpp>

#include <benchmark/benchmark.h>

#include <filesystem>
#include <fstream>

using namespace std;

namespace {

void dump(const filesystem::path& dir, string_view name, const vector<string>& data) {
    ofstream file(dir / (string(name) + ".txt"), ios::binary);
    for (const string& item : data) {
        file << item << '\n';
    }
}

}

int main(int argc, char** argv) {
    constexpr string_view dumpFlag = "--dump-data=";
    if (argc == 2 && string_view(argv[1]).starts_with(dumpFlag)) {
        const filesystem::path dir(string_view(argv[1]).substr(dumpFlag.size()));
        filesystem::create_directories(dir);
        dump(dir, "cpf", bench::generateCpfs());
        dump(dir, "cnpj", bench::generateNumericCnpjs());
        dump(dir, "cnpj_alnum", bench::generateAlphanumericCnpjs());
        dump(dir, "boleto", bench::generateBoletos());
        return 0;
    }
    benchmark::AddCustomContext("celerpay_simd", celerpay::simdBackend());
    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
        return 1;
    }
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
}
