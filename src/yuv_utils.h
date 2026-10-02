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

// 将 limited range (Y 16-235 / CbCr 16-240) 原地扩展为 full range (0-255)。
// TurboJPEG 直接把输入 YCbCr 写入 JPEG（JFIF 语义为 full range），
// 若视频帧的 limited range 不经扩展就编码，纯黑(Y=16)解码后会显示为深灰。
//
// 转换公式（四舍五入）：
//   Y:  full = clamp((limited - 16) * 255 / 219, 0, 255)
//   UV: full = clamp((limited - 16) * 255 / 224, 0, 255)
//
// 用 Q14 定点乘法实现（scale 需放入 int16，故不用 Q15）：
//   scale = round(255/div * 16384)，result = (v * scale + 8192) >> 14
//   Y  scale = 19078  (255/219 ≈ 1.16438)
//   UV scale = 18656  (255/224 ≈ 1.13839)
static inline void ExpandRangePlane(uint8_t* plane, int stride, int width, int height, bool isLuma) {
    const int16_t scale = isLuma ? 19078 : 18656;  // Q14，均 < 32768

#if defined(__aarch64__) || defined(_M_ARM64)
    const int16x4_t vscale = vdup_n_s16(scale);
    const int32x4_t vround = vdupq_n_s32(1 << 13);  // 8192，Q14 四舍五入
    for (int y = 0; y < height; ++y) {
        uint8_t* row = plane + static_cast<size_t>(y) * stride;
        int x = 0;
        for (; x <= width - 8; x += 8) {
            uint8x8_t in = vld1_u8(row + x);
            int16x8_t v = vreinterpretq_s16_u16(vmovl_u8(in));
            v = vsubq_s16(v, vdupq_n_s16(16));
            // int16 * int16 → int32，避免 Q15 溢出
            int32x4_t plo = vmull_s16(vget_low_s16(v), vscale);
            int32x4_t phi = vmull_s16(vget_high_s16(v), vscale);
            plo = vshrq_n_s32(vaddq_s32(plo, vround), 14);
            phi = vshrq_n_s32(vaddq_s32(phi, vround), 14);
            int16x8_t r = vcombine_s16(vqmovn_s32(plo), vqmovn_s32(phi));
            vst1_u8(row + x, vqmovun_s16(r));  // int16→uint8，钳到 [0,255]
        }
        for (; x < width; ++x) {
            int v = row[x] - 16;
            int r = (v * scale + 8192) >> 14;
            row[x] = r < 0 ? 0 : (r > 255 ? 255 : static_cast<uint8_t>(r));
        }
    }
#elif defined(__x86_64__) || defined(_M_X64)
    static const bool kSSE41 = CpuSupportsSSE41();
    const __m128i v16 = _mm_set1_epi16(16);
    const __m128i vscale32 = _mm_set1_epi32(scale);
    const __m128i vround = _mm_set1_epi32(1 << 13);
    const __m128i zero = _mm_setzero_si128();
    for (int y = 0; y < height; ++y) {
        uint8_t* row = plane + static_cast<size_t>(y) * stride;
        int x = 0;
        if (kSSE41) {
            for (; x <= width - 8; x += 8) {
                __m128i in = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(row + x));
                __m128i v = _mm_unpacklo_epi8(in, zero);  // 8 个 int16
                v = _mm_sub_epi16(v, v16);
                __m128i vlo = _mm_cvtepi16_epi32(v);                          // 低 4 → int32
                __m128i vhi = _mm_cvtepi16_epi32(_mm_srli_si128(v, 8));        // 高 4 → int32
                __m128i slo = _mm_srai_epi32(_mm_add_epi32(_mm_mullo_epi32(vlo, vscale32), vround), 14);
                __m128i shi = _mm_srai_epi32(_mm_add_epi32(_mm_mullo_epi32(vhi, vscale32), vround), 14);
                __m128i r16 = _mm_packs_epi32(slo, shi);                       // int32→int16
                __m128i out = _mm_packus_epi16(r16, zero);                     // int16→uint8，钳[0,255]
                _mm_storel_epi64(reinterpret_cast<__m128i*>(row + x), out);
            }
        }
        for (; x < width; ++x) {
            int v = row[x] - 16;
            int r = (v * scale + 8192) >> 14;
            row[x] = r < 0 ? 0 : (r > 255 ? 255 : static_cast<uint8_t>(r));
        }
    }
#else
    for (int y = 0; y < height; ++y) {
        uint8_t* row = plane + static_cast<size_t>(y) * stride;
        for (int x = 0; x < width; ++x) {
            int v = row[x] - 16;
            int r = (v * scale + 8192) >> 14;
            row[x] = r < 0 ? 0 : (r > 255 ? 255 : static_cast<uint8_t>(r));
        }
    }
#endif
}
