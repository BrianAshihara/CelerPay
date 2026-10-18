#include "simd/kernels.hpp"

using namespace std;

namespace celerpay::simd {

namespace {

void documentSums(const char* records, size_t count, const DocumentLayout& layout, DocumentSums* out) {
    for (size_t r = 0; r < count; ++r) {
        const char* record = records + r * 16;
        DocumentSums sums{1, 0, 0};
        for (size_t i = 0; i < 16; ++i) {
            const auto value = static_cast<uint8_t>(record[i] - '0');
            const bool digit = value <= 9;
            const bool upper = static_cast<uint8_t>(value - 17) <= 25;
            if ((layout.digit[i] != 0 && !digit) || (layout.alnum[i] != 0 && !digit && !upper)) {
                sums.valid = 0;
            }
            sums.first += value * layout.weights1[i];
            sums.second += value * layout.weights2[i];
        }
        out[r] = sums;
    }
}

bool allDigits(const char* data, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        if (static_cast<uint8_t>(data[i] - '0') > 9) {
            return false;
        }
    }
    return true;
}

constexpr Kernels kScalar{"scalar", documentSums, allDigits};

}

const Kernels& scalarKernels() noexcept { return kScalar; }

}
