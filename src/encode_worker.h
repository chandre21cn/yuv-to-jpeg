#pragma once
#include "common.h"
#include <napi.h>
#include <string>

class PixelBufferEncoder;
class EncodeContext;

// 在 libuv 线程池中编码，完成后在主线程 resolve Promise
class EncodeWorker : public Napi::AsyncWorker {
public:
    enum class Output { Jpeg, Blob };

    EncodeWorker(Napi::Env env, PixelBufferEncoder* owner, Napi::Object ownerObj,
                 Napi::Uint8Array data, const FrameLayout& layout, int quality, Output output);

    Napi::Promise Promise() { return deferred_.Promise(); }

protected:
    void Execute() override;
    void OnOK() override;
    void OnError(const Napi::Error& e) override;

private:
    Napi::Promise::Deferred deferred_;
    PixelBufferEncoder* owner_;
    Napi::ObjectReference ownerRef_;          // 保持编码器对象存活
    Napi::Reference<Napi::ArrayBuffer> abRef_;// 保持像素缓冲区存活
    uint8_t* data_;
    size_t len_;
    FrameLayout layout_;
    int quality_;
    Output output_;
    std::string errorMsg_;
};