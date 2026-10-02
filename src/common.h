#pragma once
#include <cstddef>
#include <cstdint>

#define EVEN_FLOOR(x) ((x) & ~1)

struct EncoderOptions {
    int    thread = 2;
    int    originalDimension = 1920;
    bool   extractTimeStamp = false;
    bool   removeWatermark = false;
    double bitPixel = 6.0;
    int    bitSize = 32;
};

enum class PixelFormat { NV12, I420 };

// 缓冲区布局：Y 平面 stride*height，紧跟色度平面（总长 stride*height*3/2）
struct FrameLayout {
    int width = 0;
    int height = 0;
    int stride = 0;
    PixelFormat format = PixelFormat::NV12;
    size_t bufferSize = 0;   // 可选：直接指定缓冲区字节数（>= byteSize()），0 表示用 byteSize()
    // 输入 YUV 的颜色范围：true = full range (0-255，如 Canvas/ImageBitmap)，
    // false = limited range (Y 16-235 / CbCr 16-240，视频解码帧的默认值)。
    // limited range 会在编码前扩展为 full range，否则纯黑(Y=16)在 JPEG 中会发灰。
    bool fullRange = false;

    size_t byteSize() const { return static_cast<size_t>(stride) * height * 3 / 2; }
};