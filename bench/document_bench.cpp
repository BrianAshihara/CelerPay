#include "bench_data.hpp"
#include "simd/kernels.hpp"

#include <celerpay/document.hpp>

#include <benchmark/benchmark.h>

#include <vector>

using namespace std;
using namespace celerpay;

namespace {

template <class Parse>
void parseEach(benchmark::State& state, const vector<string>& data, Parse parse) {
    for (auto _ : state) {
        for (const string& item : data) {
            benchmark::DoNotOptimize(parse(item));
        }
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations() * data.size()));
}

template <class Doc, class Batch>
void parseAll(benchmark::State& state, const vector<string>& data, Batch batch) {
    const auto inputs = bench::views(data);
    vector<Result<Doc>> outputs(inputs.size());
    for (auto _ : state) {
        benchmark::DoNotOptimize(batch(inputs, outputs));
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations() * data.size()));
}

void cpfSingle(benchmark::State& state) { parseEach(state, bench::cpfs(), parseCpf); }
void cpfBatch(benchmark::State& state) { parseAll<Cpf>(state, bench::cpfs(), parseCpfBatch); }
void cnpjSingle(benchmark::State& state) { parseEach(state, bench::cnpjs(), parseCnpj); }
void cnpjBatch(benchmark::State& state) { parseAll<Cnpj>(state, bench::cnpjs(), parseCnpjBatch); }
void cnpjAlnumSingle(benchmark::State& state) { parseEach(state, bench::alphanumericCnpjs(), parseCnpj); }
void cnpjAlnumBatch(benchmark::State& state) { parseAll<Cnpj>(state, bench::alphanumericCnpjs(), parseCnpjBatch); }

void kernel(benchmark::State& state, const simd::Kernels* kernels) {
    if (kernels == nullptr) {
        state.SkipWithMessage("kernel indisponível nesta CPU");
        return;
    }
    const auto& data = bench::cpfs();
    vector<char> records(data.size() * 16, 0);
    for (size_t i = 0; i < data.size(); ++i) {
        copy(data[i].begin(), data[i].end(), records.begin() + static_cast<ptrdiff_t>(i * 16));
    }
    simd::DocumentLayout layout;
    for (size_t i = 0; i < 11; ++i) {
        layout.digit[i] = 1;
        layout.weights1[i] = static_cast<uint8_t>(i < 9 ? 10 - i : 0);
        layout.weights2[i] = static_cast<uint8_t>(i < 10 ? 11 - i : 0);
    }
    vector<simd::DocumentSums> sums(data.size());
    for (auto _ : state) {
        kernels->documentSums(records.data(), data.size(), layout, sums.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations() * data.size()));
}

const simd::Kernels* activeIfAvx2() {
    const auto* avx2 = simd::avx2Kernels();
    return avx2 != nullptr && string_view(simd::activeKernels().name) == "avx2" ? avx2 : nullptr;
}

void kernelScalar(benchmark::State& state) { kernel(state, &simd::scalarKernels()); }
void kernelAvx2(benchmark::State& state) { kernel(state, activeIfAvx2()); }
void kernelNeon(benchmark::State& state) { kernel(state, simd::neonKernels()); }

}

BENCHMARK(cpfSingle)->Unit(benchmark::kMillisecond);
BENCHMARK(cpfBatch)->Unit(benchmark::kMillisecond);
BENCHMARK(cnpjSingle)->Unit(benchmark::kMillisecond);
BENCHMARK(cnpjBatch)->Unit(benchmark::kMillisecond);
BENCHMARK(cnpjAlnumSingle)->Unit(benchmark::kMillisecond);
BENCHMARK(cnpjAlnumBatch)->Unit(benchmark::kMillisecond);
BENCHMARK(kernelScalar)->Unit(benchmark::kMillisecond);
BENCHMARK(kernelAvx2)->Unit(benchmark::kMillisecond);
BENCHMARK(kernelNeon)->Unit(benchmark::kMillisecond);
