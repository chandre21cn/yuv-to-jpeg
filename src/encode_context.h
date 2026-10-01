#pragma once
#include "common.h"
#include "watermark.h"

#include <memory>
#include <string>
#include <vector>
#include <turbojpeg.h>

// 纯 C++ 的编码流水线（不依赖 N-API），同一时刻只能被一个线程使用。
class EncodeContext {
public:
    explicit EncodeContext(const EncoderOptions& opts) : opts_(opts) {}
    ~EncodeContext();

    EncodeContext(const EncodeContext&) = delete;
    EncodeContext& operator=(const EncodeContext&) = delete;

    void ResetGeometry() { cache_ = GeoCache{}; }

    // 提取时间戳 → (NV12 拆分) → 去水印 → JPEG 压缩。
    // 去水印会原地修改 data。成功返回 true；失败返回 false 并填充 error。
    bool Encode(uint8_t* data, size_t len, const FrameLayout& layout, int quality, std::string& error);

    bool     HasTimestamp() const { return hasTs_; }
    uint32_t Timestamp() const { return timestamp_; }
    const uint8_t* JpegData() const { return jpeg_.get(); }
    size_t   JpegSize() const { return jpegSize_; }

private:
    EncoderOptions opts_;
    GeoCache cache_;

    bool hasTs_ = false;
    uint32_t timestamp_ = 0;

    std::vector<uint8_t> u_, v_;          // NV12 拆分用
    std::unique_ptr<uint8_t[]> jpeg_;
    size_t jpegCap_ = 0;
    size_t jpegSize_ = 0;
    tjhandle tj_ = nullptr;
};