#pragma once
#include "common.h"
#include "encode_context.h"
#include "encode_worker.h"

#include <memory>
#include <napi.h>

class PixelBufferEncoder : public Napi::ObjectWrap<PixelBufferEncoder> {
public:
    static Napi::Function Define(Napi::Env env);
    explicit PixelBufferEncoder(const Napi::CallbackInfo& info);

    bool InUse() const { return inUse_; }
    EncodeContext& Context() { return *ctx_; }

    // JpegEncoder::getEncoder 调用：配置并占用（缓冲区尺寸不变时复用）
    void Acquire(Napi::Env env, const FrameLayout& layout);
    // EncodeWorker 在主线程完成时调用
    void FinishEncode(bool hasTs, uint32_t ts);

private:
    Napi::Value GetWidth(const Napi::CallbackInfo& info);
    Napi::Value GetHeight(const Napi::CallbackInfo& info);
    Napi::Value GetData(const Napi::CallbackInfo& info);
    Napi::Value GetTimestamp(const Napi::CallbackInfo& info);
    Napi::Value SetStride(const Napi::CallbackInfo& info);
    Napi::Value ToJpeg(const Napi::CallbackInfo& info);
    Napi::Value ToBlob(const Napi::CallbackInfo& info);
    Napi::Value Release(const Napi::CallbackInfo& info);
    Napi::Value StartEncode(const Napi::CallbackInfo& info, EncodeWorker::Output output);

    std::unique_ptr<EncodeContext> ctx_;
    Napi::Reference<Napi::Uint8Array> data_;
    FrameLayout layout_;
    bool inUse_ = false;
    bool busy_ = false;             // 有编码任务在线程池中
    bool releasePending_ = false;   // 编码期间调用了 release()
    bool hasTs_ = false;
    uint32_t timestamp_ = 0;
};