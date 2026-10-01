#include "watermark.h"
#include "common.h"
#include "simd_utils.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

// 横屏：bit 沿 x 排列，上块/下块；竖屏：bit 沿 y 排列，左块/右块；bit=1：第一块更亮
static int ReadBits(const uint8_t* buf, size_t bufLen, int stride, int w, int h, double unit,
                    bool vertical, int bits, uint32_t& out, double* score) {
    out = 0;
    int valid = 0;
    if (score) *score = 0.0;
    const double m = unit * 0.2;   // 只采样块中心，避开边缘模糊
    const int maxA = vertical ? h : w;
    const int maxB = vertical ? w : h;

    auto box = [&](int x0, int x1, int y0, int y1) -> uint32_t {
        uint32_t s = 0;
        for (int y = y0; y < y1; y++)
            s += FastSum(buf + static_cast<size_t>(y) * stride + x0, x1 - x0);
        return s;
    };

    for (int bit = 0; bit < bits; bit++) {
        int a0 = static_cast<int>(std::round(bit * unit + m));
        int a1 = static_cast<int>(std::round((bit + 1) * unit - m));
        if (a1 <= a0) a1 = a0 + 1;

        int t0 = static_cast<int>(std::round(m));
        int t1 = static_cast<int>(std::round(unit - m));
        if (t1 <= t0) t1 = t0 + 1;
        int tn = t1 - t0;

        int b0 = static_cast<int>(std::round(unit + m));
        if (b0 < t1) b0 = t1;
        int b1 = b0 + tn;

        if (a1 > maxA || b1 > maxB) return 0;
        {
            size_t rows = static_cast<size_t>(vertical ? a1 : b1);
            size_t cols = static_cast<size_t>(vertical ? b1 : a1);
            if ((rows - 1) * static_cast<size_t>(stride) + cols > bufLen) return 0;
        }

        uint32_t s1, s2;
        if (!vertical) { s1 = box(a0, a1, t0, t1); s2 = box(a0, a1, b0, b1); }
        else           { s1 = box(t0, t1, a0, a1); s2 = box(b0, b1, a0, a1); }

        if (s1 > s2) out |= (1U << bit);

        double area = static_cast<double>(a1 - a0) * tn;
        double diffAvg = std::abs(static_cast<double>(s1) - static_cast<double>(s2)) / area;
        if (diffAvg > 40.0) valid++;
        if (score) *score += (std::min)(diffAvg, 220.0);
    }
    return valid;
}

WmResult ExtractTimeStamp(const uint8_t* buf, size_t len, int stride, int w, int h,
                          double expected, int bits, GeoCache& cache) {
    WmResult best;
    if (!buf || expected <= 0.0) return best;
    const double good = bits * 150.0;
    const int minValid = (std::max)(1, bits / 2);

    auto tryUnit = [&](double unit) {
        for (int vert = 0; vert < 2; vert++) {
            uint32_t v = 0; double sc = 0.0;
            int n = ReadBits(buf, len, stride, w, h, unit, vert != 0, bits, v, &sc);
            if (sc > best.score) {
                best.ts = v; best.vertical = (vert != 0); best.unit = unit; best.valid = n; best.score = sc;
            }
        }
    };

    // 快路径：使用缓存的几何参数
    if (cache.has) {
        uint32_t v = 0; double sc = 0.0;
        int n = ReadBits(buf, len, stride, w, h, cache.unit, cache.vertical, bits, v, &sc);
        if (n == bits && sc > good) {
            cache.misses = 0;
            WmResult r;
            r.ts = v; r.vertical = cache.vertical; r.unit = cache.unit; r.valid = n; r.score = sc;
            return r;
        }
        if (++cache.misses >= 10) cache.has = false;   // 连续失败多次才丢弃
        best.ts = v; best.vertical = cache.vertical; best.unit = cache.unit; best.valid = n; best.score = sc;
        tryUnit(expected);
        if (best.valid < minValid) return WmResult{};
        return best;
    }

    // 慢路径：期望值 → 全范围扫描 → 细化
    tryUnit(expected);
    if (!(best.valid == bits && best.score > good)) {
        for (double u = 1.5; u <= 12.0; u += 0.03125) {
            if (std::fabs(u - expected) < 1e-6) continue;
            tryUnit(u);
        }
        const double center = best.unit;
        if (center > 0.0)
            for (double u = center - 0.04; u <= center + 0.04; u += 0.004) tryUnit(u);
    }

    if (best.valid < minValid) return WmResult{};
    if (best.valid == bits && best.score > good) {
        cache.has = true; cache.unit = best.unit; cache.vertical = best.vertical; cache.misses = 0;
    }
    return best;
}

void RemoveWatermarkPlanar(uint8_t* y, int ys, uint8_t* u, int us, uint8_t* v, int vs,
                           int w, int h, double unit, bool vertical, int bits) {
    if (unit <= 0.0) return;
    int wmW = EVEN_FLOOR(static_cast<int>(std::ceil(bits * unit) + 2));
    int wmH = EVEN_FLOOR(static_cast<int>(std::ceil(2.0 * unit) + 2));
    if (vertical) std::swap(wmW, wmH);
    wmW = (std::min)(w, wmW);
    wmH = (std::min)(h, wmH);
    if (wmW <= 0 || wmH <= 0) return;

    int sampleY = wmH + 2;
    int rowsY = (std::min)(wmH, h - sampleY);
    for (int r = 0; r < rowsY; r++)
        std::memcpy(y + static_cast<size_t>(r) * ys, y + static_cast<size_t>(sampleY + r) * ys, wmW);

    int sampleC = (wmH / 2) + 1;
    int rowsC = (std::min)(wmH / 2, h / 2 - sampleC);
    int copyC = wmW / 2;
    for (int r = 0; r < rowsC; r++) {
        std::memcpy(u + static_cast<size_t>(r) * us, u + static_cast<size_t>(sampleC + r) * us, copyC);
        std::memcpy(v + static_cast<size_t>(r) * vs, v + static_cast<size_t>(sampleC + r) * vs, copyC);
    }
}