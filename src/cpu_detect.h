#pragma once
#include <cstdint>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

// 运行时检测 CPU 是否支持 AVX2（仅 x86/x64 有意义）
// 用于在不支持 AVX2 的老机器上自动降级到 SSE2 路径，避免非法指令崩溃
static inline bool CpuSupportsAVX2() {
#if defined(__x86_64__) || defined(_M_X64)
  #if defined(__GNUC__) || defined(__clang__)
    __builtin_cpu_init();
    return __builtin_cpu_supports("avx2") != 0;
  #elif defined(_MSC_VER)
    int info[4] = {0, 0, 0, 0};
    __cpuid(info, 0);
    if (info[0] >= 7) {
      int info7[4];
      __cpuid(info7, 7);
      // AVX2: CPUID leaf 7, subleaf 0, EBX bit 5
      if ((info7[1] & (1 << 5)) == 0) return false;
      // 还需确认 OS 支持 AVX（XSAVE）
      __cpuid(info, 1);
      return (info[2] & (1 << 28)) != 0;   // OSXSAVE
    }
    return false;
  #else
    return false;
  #endif
#else
    return false;  // ARM 等平台不走 x86 SIMD 路径
#endif
}

// 运行时检测 CPU 是否支持 SSSE3（仅 x86/x64 有意义）
// SSSE3 的 _mm_mulhrs_epi16 可做带四舍五入的 Q15 乘法
static inline bool CpuSupportsSSSE3() {
#if defined(__x86_64__) || defined(_M_X64)
  #if defined(__GNUC__) || defined(__clang__)
    __builtin_cpu_init();
    return __builtin_cpu_supports("ssse3") != 0;
  #elif defined(_MSC_VER)
    int info[4] = {0, 0, 0, 0};
    __cpuid(info, 1);
    return (info[2] & (1 << 9)) != 0;   // SSSE3: ECX bit 9
  #else
    return false;
  #endif
#else
    return false;
#endif
}

// 运行时检测 CPU 是否支持 SSE4.1（仅 x86/x64 有意义）
// SSE4.1 的 _mm_cvtepi16_epi32 / _mm_mullo_epi32 用于 32 位中间值乘法
static inline bool CpuSupportsSSE41() {
#if defined(__x86_64__) || defined(_M_X64)
  #if defined(__GNUC__) || defined(__clang__)
    __builtin_cpu_init();
    return __builtin_cpu_supports("sse4.1") != 0;
  #elif defined(_MSC_VER)
    int info[4] = {0, 0, 0, 0};
    __cpuid(info, 1);
    return (info[2] & (1 << 19)) != 0;   // SSE4.1: ECX bit 19
  #else
    return false;
  #endif
#else
    return false;
#endif
}
