#include "encode_worker.h"
#include "encode_context.h"
#include "pixel_buffer_encoder.h"

#include <cstring>

EncodeWorker::EncodeWorker(Napi::Env env, PixelBufferEncoder* owner, Napi::Object ownerObj,
                           Napi::Uint8Array data, const FrameLayout& layout, int quality, Output output)
    : Napi::AsyncWorker(env),
      deferred_(Napi::Promise::Deferred::New(env)),
      owner_(owner),
      ownerRef_(Napi::Persistent(ownerObj)),
      abRef_(Napi::Persistent(data.ArrayBuffer())),
      data_(data.Data()),
      len_(data.ByteLength()),
      layout_(layout),
      quality_(quality),
      output_(output) {}

void EncodeWorker::Execute() {
    std::string err;
    if (!owner_->Context().Encode(data_, len_, layout_, quality_, err)) SetError(err);
}

void EncodeWorker::OnOK() {
    Napi::Env env = Env();
    EncodeContext& ctx = owner_->Context();
    owner_->FinishEncode(ctx.HasTimestamp(), ctx.Timestamp());

    // 拷贝为 V8 管理的 Uint8Array（不使用 external buffer）
    const size_t n = ctx.JpegSize();
    Napi::ArrayBuffer ab = Napi::ArrayBuffer::New(env, n);
    std::memcpy(ab.Data(), ctx.JpegData(), n);
    Napi::Uint8Array jpeg = Napi::Uint8Array::New(env, n, ab, 0);

    if (output_ == Output::Jpeg) {
        deferred_.Resolve(jpeg);
        return;
    }

    // Blob：new Blob([jpeg], { type: 'image/jpeg' })
    Napi::Value blobCtor = env.Global().Get("Blob");
    if (!blobCtor.IsFunction()) {
        deferred_.Reject(Napi::Error::New(env, "Blob is not available in this runtime").Value());
        return;
    }
    Napi::Array parts = Napi::Array::New(env, 1);
    parts.Set(static_cast<uint32_t>(0), jpeg);
    Napi::Object bag = Napi::Object::New(env);
    bag.Set("type", "image/jpeg");
    Napi::Value blob = blobCtor.As<Napi::Function>().New({ parts, bag });
    if (env.IsExceptionPending()) {
        deferred_.Reject(env.GetAndClearPendingException().Value());
        return;
    }
    deferred_.Resolve(blob);
}

void EncodeWorker::OnError(const Napi::Error& e) {
    owner_->FinishEncode(false, 0);
    deferred_.Reject(e.Value());
}