#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace celerpay::simd {

struct DocumentLayout {
    std::array<std::uint8_t, 16> digit{};
    std::array<std::uint8_t, 16> alnum{};
    std::array<std::uint8_t, 16> weights1{};
    std::array<std::uint8_t, 16> weights2{};
};

struct DocumentSums {
    std::uint32_t valid = 0;
    std::int32_t first = 0;
    std::int32_t second = 0;
};

using DocumentSumsFn = void (*)(const char* records, std::size_t count, const DocumentLayout& layout, DocumentSums* out);
using AllDigitsFn = bool (*)(const char* data, std::size_t size);

struct Kernels {
    const char* name;
    DocumentSumsFn documentSums;
    AllDigitsFn allDigits;
};

const Kernels& scalarKernels() noexcept;
const Kernels* avx2Kernels() noexcept;
const Kernels* neonKernels() noexcept;
const Kernels& activeKernels() noexcept;

}
