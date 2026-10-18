#include "support/synthetic.hpp"

#include <celerpay/reconcile.hpp>

#include <benchmark/benchmark.h>

using namespace std;
using namespace celerpay;

namespace {

void reconcileSynthetic(benchmark::State& state) {
    const auto calendar = rules::Calendar::brazil();
    const auto data = synthetic::generate(static_cast<size_t>(state.range(0)), calendar);
    reconcile::Options options;
    options.referenceDate = data.reference;
    size_t matched = 0;
    for (auto _ : state) {
        const auto report = reconcile::run(data.charges, data.payments, options, calendar);
        matched = report->count(reconcile::Status::Matched);
        benchmark::DoNotOptimize(matched);
    }
    state.counters["charges"] = static_cast<double>(data.charges.size());
    state.counters["payments"] = static_cast<double>(data.payments.size());
    state.counters["matched_groups"] = static_cast<double>(matched);
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations() * (data.charges.size() + data.payments.size())));
}

}

BENCHMARK(reconcileSynthetic)->Arg(10'000)->Arg(100'000)->Arg(1'000'000)->Unit(benchmark::kMillisecond)->Iterations(1)->UseRealTime();
