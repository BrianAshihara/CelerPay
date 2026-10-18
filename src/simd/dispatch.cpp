#include "simd/kernels.hpp"

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h>
#endif

namespace celerpay::simd {

namespace {

bool cpuHasAvx2() noexcept {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    int info[4];
    __cpuid(info, 0);
    if (info[0] < 7) {
        return false;
    }
    __cpuidex(info, 1, 0);
    const bool osxsave = (info[2] & (1 << 27)) != 0;
    const bool avx = (info[2] & (1 << 28)) != 0;
    if (!osxsave || !avx || (_xgetbv(0) & 0x6) != 0x6) {
        return false;
    }
    __cpuidex(info, 7, 0);
    return (info[1] & (1 << 5)) != 0;
#elif (defined(__GNUC__) || defined(__clang__)) && (defined(__x86_64__) || defined(__i386__))
    return __builtin_cpu_supports("avx2");
#else
    return false;
#endif
}

const Kernels& select() noexcept {
    if (const Kernels* neon = neonKernels()) {
        return *neon;
    }
    if (const Kernels* avx2 = avx2Kernels(); avx2 != nullptr && cpuHasAvx2()) {
        return *avx2;
    }
    return scalarKernels();
}

}

const Kernels& activeKernels() noexcept {
    static const Kernels& kernels = select();
    return kernels;
}

}
