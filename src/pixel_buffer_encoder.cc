#include "pixel_buffer_encoder.h"

#include <algorithm>

static Napi::Value RejectedPromise(Napi::Env env, const char* msg) {
    auto d = Napi::Promise::Deferred::New(env);
    d.Reject(Napi::Error::New(env, msg).Value());
    return d.Promise();
}

Napi::Function PixelBufferEncoder::Define(Napi::Env env) {
    return DefineClass(env, "PixelBufferEncoder", {
        InstanceAccessor("width",     &PixelBufferEncoder::GetWidth,     nullptr),
        InstanceAccessor("height",    &PixelBufferEncoder::GetHeight,    nullptr),
        InstanceAccessor("data",      &PixelBufferEncoder::GetData,      nullptr),
        InstanceAccessor("timestamp", &PixelBufferEncoder::GetTimestamp, nullptr),
        InstanceMethod("toJpeg",  &PixelBufferEncoder::ToJpeg),
        InstanceMethod("toBlob",  &PixelBufferEncoder::ToBlob),
        InstanceMethod("setStride", &PixelBufferEncoder::SetStride),
        InstanceMethod("release", &PixelBufferEncoder::Release),
    });
}

PixelBufferEncoder::PixelBufferEncoder(const Napi::CallbackInfo& info)
    : Napi::ObjectWrap<PixelBufferEncoder>(info) {
    Napi::Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsExternal()) {
        Napi::TypeError::New(env, "PixelBufferEncoder cannot be constructed directly").ThrowAsJavaScriptException();
        return;
    }
    ctx_ = std::make_unique<EncodeContext>(*info[0].As<Napi::External<EncoderOptions>>().Data());
}

void PixelBufferEncoder::Acquire(Napi::Env env, const FrameLayout& layout) {
    const size_t minSize = layout.byteSize();
    // bufferSize 由调用方传入（如 frame.allocationSize()），保证装得下带 stride 填充的整帧数据
    const size_t need = layout.bufferSize > minSize ? layout.bufferSize : minSize;
    const bool same = !data_.IsEmpty() &&
                      layout.width == layout_.width && layout.height == layout_.height &&
                      layout.stride == layout_.stride && layout.format == layout_.format &&
                      data_.Value().ByteLength() == need;
    if (!same) {
        layout_ = layout;
        data_.Reset();
        data_ = Napi::Persistent(Napi::Uint8Array::New(env, need));
        ctx_->ResetGeometry();
    }
    hasTs_ = false;
    timestamp_ = 0;
    inUse_ = true;
    releasePending_ = false;
}

void PixelBufferEncoder::FinishEncode(bool hasTs, uint32_t ts) {
    busy_ = false;
    hasTs_ = hasTs;
    timestamp_ = ts;
    if (releasePending_) {
        releasePending_ = false;
        inUse_ = false;
        hasTs_ = false;
    }
}

Napi::Value PixelBufferEncoder::GetWidth(const Napi::CallbackInfo& info)  { return Napi::Number::New(info.Env(), layout_.width); }
Napi::Value PixelBufferEncoder::GetHeight(const Napi::CallbackInfo& info) { return Napi::Number::New(info.Env(), layout_.height); }

Napi::Value PixelBufferEncoder::GetData(const Napi::CallbackInfo& info) {
    if (data_.IsEmpty()) return info.Env().Undefined();
    return data_.Value();
}

Napi::Value PixelBufferEncoder::GetTimestamp(const Napi::CallbackInfo& info) {
    if (!hasTs_) return info.Env().Undefined();
    return Napi::Number::New(info.Env(), timestamp_);
}

// frame.copyTo(data) 后调用，把平台实际的 Y stride 写回编码器。
// 分配时可能用了偏大的 stride（保证缓冲区装得下），编码时要用真实 stride 寻址。
Napi::Value PixelBufferEncoder::SetStride(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsNumber()) {
        Napi::TypeError::New(env, "setStride expects a number").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    int stride = info[0].As<Napi::Number>().Int32Value();
    if (stride < layout_.width || (stride & 1)) {
        Napi::RangeError::New(env, "stride must be an even number >= width").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    layout_.stride = stride;
    return env.Undefined();
}

Napi::Value PixelBufferEncoder::Release(const Napi::CallbackInfo& info) {
    if (busy_) {
        releasePending_ = true;   // 编码完成后再归还
    } else {
        inUse_ = false;
        hasTs_ = false;
    }
    return info.Env().Undefined();
}

Napi::Value PixelBufferEncoder::ToJpeg(const Napi::CallbackInfo& info) { return StartEncode(info, EncodeWorker::Output::Jpeg); }
Napi::Value PixelBufferEncoder::ToBlob(const Napi::CallbackInfo& info) { return StartEncode(info, EncodeWorker::Output::Blob); }

Napi::Value PixelBufferEncoder::StartEncode(const Napi::CallbackInfo& info, EncodeWorker::Output output) {
    Napi::Env env = info.Env();
    if (!inUse_ || releasePending_ || data_.IsEmpty())
        return RejectedPromise(env, "Encoder has been released");
    if (busy_)
        return RejectedPromise(env, "Encoder is busy: previous encode has not finished");

    int quality = 80;
    if (info.Length() >= 1 && info[0].IsNumber()) quality = info[0].As<Napi::Number>().Int32Value();
    quality = (std::min)(100, (std::max)(1, quality));

    Napi::Uint8Array arr = data_.Value();
    if (arr.ByteLength() != layout_.byteSize())
        return RejectedPromise(env, "Pixel buffer has been detached or resized");

    auto* worker = new EncodeWorker(env, this, info.This().As<Napi::Object>(), arr, layout_, quality, output);
    Napi::Promise promise = worker->Promise();
    busy_ = true;
    worker->Queue();
    return promise;
}