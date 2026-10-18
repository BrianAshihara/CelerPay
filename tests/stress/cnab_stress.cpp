#include <celerpay/statement.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif

using namespace std;
using namespace celerpay;
using namespace celerpay::statement;

namespace {

constexpr uint64_t kTarget = 1ull << 30;

struct Memory {
    uint64_t privateBytes = 0;
    uint64_t peakPrivateBytes = 0;
};

Memory memory() {
    Memory out;
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters))) {
        out.privateBytes = counters.PrivateUsage;
        out.peakPrivateBytes = counters.PeakPagefileUsage;
    }
#elif defined(__linux__)
    ifstream status("/proc/self/status");
    for (string line; getline(status, line);) {
        if (line.starts_with("VmRSS:")) {
            out.privateBytes = stoull(line.substr(6)) * 1024;
        } else if (line.starts_with("VmHWM:")) {
            out.peakPrivateBytes = stoull(line.substr(6)) * 1024;
        }
    }
#endif
    return out;
}

string record(char type) {
    string r(240, ' ');
    r.replace(0, 3, "001");
    r[7] = type;
    return r;
}

void put(string& r, size_t start, string_view value) { r.replace(start - 1, value.size(), value); }

string pair(uint64_t index) {
    string t = record('3');
    const string number = to_string(index);
    put(t, 14, "T");
    put(t, 16, "06");
    put(t, 38, string(20 - number.size(), '0') + number);
    put(t, 59, "DOC" + to_string(index % 10000));
    put(t, 74, "20032025");
    put(t, 82, "000000000010000");
    put(t, 133, "1");
    put(t, 134, "000052998224725");
    put(t, 149, "FULANO DE TAL");
    put(t, 199, string(15, '0'));
    string u = record('3');
    put(u, 14, "U");
    put(u, 16, "06");
    for (const size_t start : {size_t{18}, size_t{33}, size_t{48}, size_t{63}, size_t{93}, size_t{108}, size_t{123}}) {
        put(u, start, string(15, '0'));
    }
    put(u, 78, "000000000010000");
    put(u, 138, "21032025");
    put(u, 146, "24032025");
    return t + "\r\n" + u + "\r\n";
}

uint64_t generate(const filesystem::path& path) {
    ofstream out(path, ios::binary);
    out << record('0') << "\r\n" << record('1') << "\r\n";
    uint64_t records = 2;
    uint64_t written = 484;
    string chunk;
    for (uint64_t i = 0; written < kTarget; ++i) {
        chunk += pair(i);
        records += 2;
        written += 484;
        if (chunk.size() > (1 << 20)) {
            out << chunk;
            chunk.clear();
        }
    }
    out << chunk;
    out << record('5') << "\r\n";
    string trailer = record('9');
    const string count = to_string((records + 2) % 1'000'000);
    put(trailer, 24, string(6 - count.size(), '0') + count);
    out << trailer << "\r\n";
    return records + 2;
}

void report(const char* label, const Result<Summary>& summary, chrono::steady_clock::duration elapsed, const Memory& before) {
    const Memory after = memory();
    const double seconds = chrono::duration<double>(elapsed).count();
    if (!summary) {
        printf("%s: erro %s na linha %llu: %s\n", label, string(toString(summary.error().code)).c_str(),
               static_cast<unsigned long long>(summary.error().offset),
               string(summary.error().detail).c_str());
        return;
    }
    printf("%s: %llu transacoes, total %s, %.2f s, %.0f MB/s, memoria privada antes %.1f MB, depois %.1f MB, pico %.1f MB\n", label,
           static_cast<unsigned long long>(summary->transactions), formatBrl(summary->total).c_str(), seconds,
           static_cast<double>(kTarget) / (1 << 20) / seconds, static_cast<double>(before.privateBytes) / (1 << 20),
           static_cast<double>(after.privateBytes) / (1 << 20), static_cast<double>(after.peakPrivateBytes) / (1 << 20));
}

}

int main(int argc, char** argv) {
    const filesystem::path path = argc > 1 ? filesystem::path(argv[1]) : filesystem::temp_directory_path() / "celerpay_stress_1gb.ret";
    if (!filesystem::exists(path) || filesystem::file_size(path) < kTarget) {
        printf("gerando %s\n", path.string().c_str());
        generate(path);
    }
    printf("arquivo: %.1f MB\n", static_cast<double>(filesystem::file_size(path)) / (1 << 20));

    uint64_t seen = 0;
    const Sink sink = [&](const Transaction& t) { seen += static_cast<uint64_t>(t.amount.cents() > 0); };

    {
        const Memory before = memory();
        ifstream input(path, ios::binary);
        const auto start = chrono::steady_clock::now();
        const auto summary = parseCnab240(input, cnab240::febraban(), sink);
        report("stream", summary, chrono::steady_clock::now() - start, before);
        if (!summary) {
            return 1;
        }
    }
    {
        const Memory before = memory();
        const auto start = chrono::steady_clock::now();
        const auto file = MappedFile::open(path);
        if (!file) {
            printf("erro ao mapear\n");
            return 1;
        }
        const auto summary = parseCnab240(file->data(), cnab240::febraban(), sink);
        report("mmap", summary, chrono::steady_clock::now() - start, before);
        if (!summary) {
            return 1;
        }
    }
    return 0;
}
