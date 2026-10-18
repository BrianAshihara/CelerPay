#include "bench_data.hpp"

#include <celerpay/boleto.hpp>
#include <celerpay/pix.hpp>
#ifdef CELERPAY_BENCH_QR
#include <celerpay/qr.hpp>
#endif

#include <benchmark/benchmark.h>

using namespace std;
using namespace celerpay;

namespace {

constexpr string_view kPix = "00020126580014br.gov.bcb.pix0136123e4567-e12b-12d1-a456-4266554400005204000053039865802BR5913Fulano de "
                             "Tal6008BRASILIA62070503***63041D3D";

void boletoLine(benchmark::State& state) {
    const auto& data = bench::boletos();
    for (auto _ : state) {
        for (const string& line : data) {
            benchmark::DoNotOptimize(boleto::parseLine(line));
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(data.size()));
}

void boletoBarcode(benchmark::State& state) {
    vector<string> barcodes;
    for (const string& line : bench::boletos()) {
        barcodes.emplace_back(boleto::parseLine(line)->digits());
    }
    for (auto _ : state) {
        for (const string& barcode : barcodes) {
            benchmark::DoNotOptimize(boleto::parseBarcode(barcode));
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(barcodes.size()));
}

void pixParse(benchmark::State& state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(pix::parse(kPix));
    }
    state.SetItemsProcessed(state.iterations());
}

void pixBuild(benchmark::State& state) {
    const auto payload = *pix::parse(kPix);
    for (auto _ : state) {
        benchmark::DoNotOptimize(pix::build(payload));
    }
    state.SetItemsProcessed(state.iterations());
}

void moneyParse(benchmark::State& state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(parseAmount("1234567.89"));
    }
    state.SetItemsProcessed(state.iterations());
}

void moneyFormat(benchmark::State& state) {
    const Money value = Money::fromCents(123456789);
    for (auto _ : state) {
        benchmark::DoNotOptimize(formatBrl(value));
    }
    state.SetItemsProcessed(state.iterations());
}

#ifdef CELERPAY_BENCH_QR
void qrEncode(benchmark::State& state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(qr::encode(kPix));
    }
    state.SetItemsProcessed(state.iterations());
}

void qrSvg(benchmark::State& state) {
    const auto matrix = *qr::encode(kPix);
    for (auto _ : state) {
        benchmark::DoNotOptimize(qr::toSvg(matrix));
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(qrEncode);
BENCHMARK(qrSvg);
#endif

}

BENCHMARK(boletoLine)->Unit(benchmark::kMillisecond);
BENCHMARK(boletoBarcode)->Unit(benchmark::kMillisecond);
BENCHMARK(pixParse);
BENCHMARK(pixBuild);
BENCHMARK(moneyParse);
BENCHMARK(moneyFormat);
