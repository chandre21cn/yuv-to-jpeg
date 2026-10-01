#include "encode_context.h"

#include <algorithm>
#include "yuv_utils.h"

EncodeContext::~EncodeContext() {
    if (tj_) tjDestroy(tj_);
}

bool EncodeContext::Encode(uint8_t* base, size_t len, const FrameLayout& lay, int quality, std::string& error) {
    hasTs_ = false;
    timestamp_ = 0;
    jpegSize_ = 0;

    if (!base || len < lay.byteSize()) {
        error = "Pixel buffer is smaller than required by width/height/stride";
        return false;
    }

    const int w = lay.width, h = lay.height, stride = lay.stride;
    const int uvW = w / 2, uvH = h / 2;
    const size_t yBytes = static_cast<size_t>(stride) * h;
    const bool i420 = lay.format == PixelFormat::I420;

    // 1. 时间戳 / 水印几何
    const double scale = static_cast<double>(opts_.originalDimension) / static_cast<double>((std::max)(w, h));
    const double expected = opts_.bitPixel / scale;
    WmResult wm;
    if (opts_.extractTimeStamp || opts_.removeWatermark) {
        wm = ExtractTimeStamp(base, yBytes, stride, w, h, expected, opts_.bitSize, cache_);
        if (opts_.extractTimeStamp && wm.unit > 0.0) {
            timestamp_ = wm.ts;
            hasTs_ = true;
        }
    }

    // 2. 取得 Y/U/V 平面
    uint8_t* yP = base;
    uint8_t *uP, *vP;
    int uS, vS;
    if (i420) {
        uS = vS = stride / 2;
        uP = base + yBytes;
        vP = uP + static_cast<size_t>(uS) * uvH;
    } else {
        u_.resize(static_cast<size_t>(uvW) * uvH);
        v_.resize(static_cast<size_t>(uvW) * uvH);
        uP = u_.data(); vP = v_.data();
        uS = vS = uvW;
        SplitUVPlane(base + yBytes, stride, uP, uS, vP, vS, uvW, uvH);
    }

    // 3. 去水印（未检测到时退回按 originalDimension 推算的横屏区域）
    if (opts_.removeWatermark) {
        double unit = wm.unit > 0.0 ? wm.unit : expected;
        RemoveWatermarkPlanar(yP, stride, uP, uS, vP, vS, w, h, unit, wm.vertical, opts_.bitSize);
    }

    // 4. JPEG 压缩
    if (!tj_) tj_ = tjInitCompress();
    if (!tj_) {
        error = "Failed to initialize TurboJPEG compressor";
        return false;
    }
    const size_t cap = static_cast<size_t>(tjBufSize(w, h, TJSAMP_420));
    if (jpegCap_ < cap) { jpeg_.reset(new uint8_t[cap]); jpegCap_ = cap; }

    unsigned char* jpegBuf = jpeg_.get();
    unsigned long jpegSize = static_cast<unsigned long>(jpegCap_);
    const uint8_t* planes[3] = { yP, uP, vP };
    int strides[3] = { stride, uS, vS };

    if (tjCompressFromYUVPlanes(tj_, planes, w, strides, h, TJSAMP_420, &jpegBuf, &jpegSize,
                                quality, TJFLAG_FASTDCT | TJFLAG_NOREALLOC) != 0) {
        error = std::string("TurboJPEG Compression Error: ") + tjGetErrorStr2(tj_);
        tjDestroy(tj_);   // 出错后丢弃句柄，下次重建
        tj_ = nullptr;
        return false;
    }
    jpegSize_ = jpegSize;
    return true;
}