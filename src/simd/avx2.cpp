#include "simd/kernels.hpp"

#if defined(__AVX2__)

#include <immintrin.h>

using namespace std;

namespace celerpay::simd {

namespace {

__m256i broadcastMask(const array<uint8_t, 16>& flags) noexcept {
    const __m128i raw = _mm_loadu_si128(reinterpret_cast<const __m128i*>(flags.data()));
    const __m128i mask = _mm_xor_si128(_mm_cmpeq_epi8(raw, _mm_setzero_si128()), _mm_set1_epi8(-1));
    return _mm256_broadcastsi128_si256(mask);
}

__m256i broadcastWeights(const array<uint8_t, 16>& weights) noexcept {
    return _mm256_broadcastsi128_si256(_mm_loadu_si128(reinterpret_cast<const __m128i*>(weights.data())));
}

int32_t horizontalSum(__m128i x) noexcept {
    x = _mm_add_epi32(x, _mm_shuffle_epi32(x, 0x4E));
    x = _mm_add_epi32(x, _mm_shuffle_epi32(x, 0xB1));
    return _mm_cvtsi128_si32(x);
}

void documentSums(const char* records, size_t count, const DocumentLayout& layout, DocumentSums* out) {
    const __m256i digitSel = broadcastMask(layout.digit);
    const __m256i alnumSel = broadcastMask(layout.alnum);
    const __m256i freeSel = _mm256_xor_si256(_mm256_or_si256(digitSel, alnumSel), _mm256_set1_epi8(-1));
    const __m256i w1 = broadcastWeights(layout.weights1);
    const __m256i w2 = broadcastWeights(layout.weights2);
    const __m256i zero = _mm256_set1_epi8('0');
    const __m256i nine = _mm256_set1_epi8(9);
    const __m256i seventeen = _mm256_set1_epi8(17);
    const __m256i twentyFive = _mm256_set1_epi8(25);
    const __m256i ones = _mm256_set1_epi16(1);

    size_t r = 0;
    for (; r + 2 <= count; r += 2) {
        const __m256i chars = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(records + r * 16));
        const __m256i value = _mm256_sub_epi8(chars, zero);
        const __m256i digit = _mm256_cmpeq_epi8(_mm256_min_epu8(value, nine), value);
        const __m256i letterValue = _mm256_sub_epi8(value, seventeen);
        const __m256i upper = _mm256_cmpeq_epi8(_mm256_min_epu8(letterValue, twentyFive), letterValue);
        const __m256i ok = _mm256_or_si256(
            _mm256_or_si256(_mm256_and_si256(digit, digitSel), _mm256_and_si256(_mm256_or_si256(digit, upper), alnumSel)),
            freeSel);
        const auto mask = static_cast<uint32_t>(_mm256_movemask_epi8(ok));

        const __m256i s1 = _mm256_madd_epi16(_mm256_maddubs_epi16(value, w1), ones);
        const __m256i s2 = _mm256_madd_epi16(_mm256_maddubs_epi16(value, w2), ones);

        out[r] = {(mask & 0xFFFF) == 0xFFFF ? 1u : 0u, horizontalSum(_mm256_castsi256_si128(s1)),
                  horizontalSum(_mm256_castsi256_si128(s2))};
        out[r + 1] = {(mask >> 16) == 0xFFFF ? 1u : 0u, horizontalSum(_mm256_extracti128_si256(s1, 1)),
                      horizontalSum(_mm256_extracti128_si256(s2, 1))};
    }
    if (r < count) {
        scalarKernels().documentSums(records + r * 16, count - r, layout, out + r);
    }
}

bool allDigits(const char* data, size_t size) {
    const __m256i zero = _mm256_set1_epi8('0');
    const __m256i nine = _mm256_set1_epi8(9);
    size_t i = 0;
    for (; i + 32 <= size; i += 32) {
        const __m256i value = _mm256_sub_epi8(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(data + i)), zero);
        if (_mm256_movemask_epi8(_mm256_cmpeq_epi8(_mm256_min_epu8(value, nine), value)) != -1) {
            return false;
        }
    }
    return scalarKernels().allDigits(data + i, size - i);
}

constexpr Kernels kAvx2{"avx2", documentSums, allDigits};

}

const Kernels* avx2Kernels() noexcept { return &kAvx2; }

}

#else

namespace celerpay::simd {

const Kernels* avx2Kernels() noexcept { return nullptr; }

}

#endif
