#include "simd/kernels.hpp"

#if defined(__aarch64__) || defined(_M_ARM64)

#include <arm_neon.h>

using namespace std;

namespace celerpay::simd {

namespace {

uint8x16_t selector(const array<uint8_t, 16>& flags) noexcept {
    const uint8x16_t raw = vld1q_u8(flags.data());
    return vtstq_u8(raw, raw);
}

uint32_t weightedSum(uint8x16_t value, uint8x16_t weights) noexcept {
    const uint16x8_t low = vmull_u8(vget_low_u8(value), vget_low_u8(weights));
    const uint16x8_t high = vmull_high_u8(value, weights);
    return vaddlvq_u16(low) + vaddlvq_u16(high);
}

void documentSums(const char* records, size_t count, const DocumentLayout& layout, DocumentSums* out) {
    const uint8x16_t digitSel = selector(layout.digit);
    const uint8x16_t alnumSel = selector(layout.alnum);
    const uint8x16_t freeSel = vmvnq_u8(vorrq_u8(digitSel, alnumSel));
    const uint8x16_t w1 = vld1q_u8(layout.weights1.data());
    const uint8x16_t w2 = vld1q_u8(layout.weights2.data());

    for (size_t r = 0; r < count; ++r) {
        const uint8x16_t chars = vld1q_u8(reinterpret_cast<const uint8_t*>(records + r * 16));
        const uint8x16_t value = vsubq_u8(chars, vdupq_n_u8('0'));
        const uint8x16_t digit = vcleq_u8(value, vdupq_n_u8(9));
        const uint8x16_t upper = vcleq_u8(vsubq_u8(value, vdupq_n_u8(17)), vdupq_n_u8(25));
        const uint8x16_t ok = vorrq_u8(vorrq_u8(vandq_u8(digit, digitSel), vandq_u8(vorrq_u8(digit, upper), alnumSel)), freeSel);
        out[r] = {vminvq_u8(ok) == 0xFF ? 1u : 0u, static_cast<int32_t>(weightedSum(value, w1)),
                  static_cast<int32_t>(weightedSum(value, w2))};
    }
}

bool allDigits(const char* data, size_t size) {
    size_t i = 0;
    for (; i + 16 <= size; i += 16) {
        const uint8x16_t value = vsubq_u8(vld1q_u8(reinterpret_cast<const uint8_t*>(data + i)), vdupq_n_u8('0'));
        if (vmaxvq_u8(value) > 9) {
            return false;
        }
    }
    return scalarKernels().allDigits(data + i, size - i);
}

constexpr Kernels kNeon{"neon", documentSums, allDigits};

}

const Kernels* neonKernels() noexcept { return &kNeon; }

}

#else

namespace celerpay::simd {

const Kernels* neonKernels() noexcept { return nullptr; }

}

#endif
