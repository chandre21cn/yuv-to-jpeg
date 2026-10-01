#pragma once
#include <cstddef>
#include <cstdint>
#include "cpu_detect.h"

#if defined(__x86_64__) || defined(_M_X64)
    #include <emmintrin.h>
    #include <immintrin.h>   // AVX2 intrinsic（运行时检测后才调用，编译时不强制 /arch:AVX2）
#elif defined(__aarch64__) || defined(_M_ARM64)
    #include <arm_neon.h>
#endif

// NV12 的交错 UV 平面 → 独立的 U、V 平面（替代 libyuv::SplitUVPlane）
// width 为 U/V 平面的宽度（= 图像宽度 / 2），src 每行有 width*2 字节
static inline void SplitUVPlane(const uint8_t* src, int srcStride,
                                uint8_t* dstU, int dstStrideU,
                                uint8_t* dstV, int dstStrideV,
                                int width, int height) {
#if defined(__x86_64__) || defined(_M_X64)
    static const bool kAVX2 = CpuSupportsAVX2();
#endif
    for (int y = 0; y < height; ++y) {
        const uint8_t* s = src + static_cast<size_t>(y) * srcStride;
        uint8_t* u = dstU + static_cast<size_t>(y) * dstStrideU;
        uint8_t* v = dstV + static_cast<size_t>(y) * dstStrideV;
        int x = 0;

#if defined(__x86_64__) || defined(_M_X64)
        if (kAVX2) {
            const __m256i evenMask = _mm256_setr_epi8(
                0, 2, 4, 6, 8, 10, 12, 14, -1, -1, -1, -1, -1, -1, -1, -1,
                0, 2, 4, 6, 8, 10, 12, 14, -1, -1, -1, -1, -1, -1, -1, -1);
            const __m256i oddMask = _mm256_setr_epi8(
                1, 3, 5, 7, 9, 11, 13, 15, -1, -1, -1, -1, -1, -1, -1, -1,
                1, 3, 5, 7, 9, 11, 13, 15, -1, -1, -1, -1, -1, -1, -1, -1);
            for (; x <= width - 32; x += 32) {
                __m256i a = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + 2 * x));
                __m256i b = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + 2 * x + 32));
                __m256i ua = _mm256_shuffle_epi8(a, evenMask);
                __m256i va = _mm256_shuffle_epi8(a, oddMask);
                __m256i ub = _mm256_shuffle_epi8(b, evenMask);
                __m256i vb = _mm256_shuffle_epi8(b, oddMask);
                __m128i u0 = _mm256_castsi256_si128(ua);
                __m128i u1 = _mm256_extracti128_si256(ua, 1);
                __m128i v0 = _mm256_castsi256_si128(va);
                __m128i v1 = _mm256_extracti128_si256(va, 1);
                __m128i u2 = _mm256_castsi256_si128(ub);
                __m128i u3 = _mm256_extracti128_si256(ub, 1);
                __m128i v2 = _mm256_castsi256_si128(vb);
                __m128i v3 = _mm256_extracti128_si256(vb, 1);
                _mm_storeu_si128(reinterpret_cast<__m128i*>(u + x), _mm_unpacklo_epi64(u0, u1));
                _mm_storeu_si128(reinterpret_cast<__m128i*>(v + x), _mm_unpacklo_epi64(v0, v1));
                _mm_storeu_si128(reinterpret_cast<__m128i*>(u + x + 16), _mm_unpacklo_epi64(u2, u3));
                _mm_storeu_si128(reinterpret_cast<__m128i*>(v + x + 16), _mm_unpacklo_epi64(v2, v3));
            }
        } else {
            const __m128i mask = _mm_set1_epi16(0x00FF);
            for (; x <= width - 16; x += 16) {
                __m128i a = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s + 2 * x));
                __m128i b = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s + 2 * x + 16));
                _mm_storeu_si128(reinterpret_cast<__m128i*>(u + x),
                                 _mm_packus_epi16(_mm_and_si128(a, mask), _mm_and_si128(b, mask)));
                _mm_storeu_si128(reinterpret_cast<__m128i*>(v + x),
                                 _mm_packus_epi16(_mm_srli_epi16(a, 8), _mm_srli_epi16(b, 8)));
            }
        }
#elif defined(__aarch64__) || defined(_M_ARM64)
        for (; x <= width - 16; x += 16) {
            uint8x16x2_t p = vld2q_u8(s + 2 * x);   // 硬件解交错
            vst1q_u8(u + x, p.val[0]);
            vst1q_u8(v + x, p.val[1]);
        }
#endif
        for (; x < width; ++x) {
            u[x] = s[2 * x];
            v[x] = s[2 * x + 1];
        }
    }
}
