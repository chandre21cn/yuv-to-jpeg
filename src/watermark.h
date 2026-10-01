#pragma once
#include <cstddef>
#include <cstdint>

struct WmResult {
    uint32_t ts = 0;
    bool     vertical = false;
    double   unit = 0.0;    // 检测到的块边长，0 = 未检测到
    int      valid = 0;
    double   score = 0.0;
};

// 同一分辨率下水印几何固定：缓存上一次成功的块大小/方向（每个编码器一份）
struct GeoCache {
    bool   has = false;
    double unit = 0.0;
    bool   vertical = false;
    int    misses = 0;
};

// 在 Y 平面上读取时间戳。expected = bitPixel / scale
WmResult ExtractTimeStamp(const uint8_t* y, size_t yLen, int stride, int w, int h,
                          double expected, int bits, GeoCache& cache);

// 用水印区正下方的像素覆盖水印（平面格式，原地修改）
void RemoveWatermarkPlanar(uint8_t* y, int ys, uint8_t* u, int us, uint8_t* v, int vs,
                           int w, int h, double unit, bool vertical, int bits);