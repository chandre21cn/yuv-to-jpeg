#pragma once
#include <cstdint>
#include "cpu_detect.h"

#if defined(__x86_64__) || defined(_M_X64)
    #include <emmintrin.h>
    #include <immintrin.h>
#elif defined(__aarch64__)
    #include <arm_neon.h>
#endif

// 求 len 个字节之和（水印块很窄，所以保持内联）
static inline uint32_t FastSum(const uint8_t* ptr, int len) {
    if (len <= 0) return 0;
    uint32_t sum = 0;
    int x = 0;
#if defined(__x86_64__) || defined(_M_X64)
    static const bool kAVX2 = CpuSupportsAVX2();
    if (kAVX2) {
        __m256i vSum = _mm256_setzero_si256();
        const __m256i vZero = _mm256_setzero_si256();
        for (; x <= len - 32; x += 32)
            vSum = _mm256_add_epi64(vSum, _mm256_sad_epu8(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(ptr + x)), vZero));
        __m128i lo = _mm256_castsi256_si128(vSum);
        __m128i hi = _mm256_extracti128_si256(vSum, 1);
        __m128i s128 = _mm_add_epi64(lo, hi);
        uint64_t lanes[2];
        _mm_storeu_si128(reinterpret_cast<__m128i*>(lanes), s128);
        sum += static_cast<uint32_t>(lanes[0] + lanes[1]);
    } else {
        __m128i vSum = _mm_setzero_si128();
        const __m128i vZero = _mm_setzero_si128();
        for (; x <= len - 16; x += 16)
            vSum = _mm_add_epi64(vSum, _mm_sad_epu8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(ptr + x)), vZero));
        uint64_t lanes[2];
        _mm_storeu_si128(reinterpret_cast<__m128i*>(lanes), vSum);
        sum += static_cast<uint32_t>(lanes[0] + lanes[1]);
    }
#elif defined(__aarch64__)
    for (; x <= len - 16; x += 16) sum += vaddlvq_u8(vld1q_u8(ptr + x));
#endif
    for (; x < len; ++x) sum += ptr[x];
    return sum;
}
