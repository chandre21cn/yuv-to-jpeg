#include "jpeg_encoder.h"
#include "addon_data.h"
#include "pixel_buffer_encoder.h"

#include <string>

namespace {

template <typename T>
bool ReadNum(Napi::Env env, Napi::Object o, const char* k, T& dst) {
    if (!o.Has(k)) return true;
    Napi::Value v = o.Get(k);
    if (v.IsUndefined()) return true;
    if (!v.IsNumber()) {
        Napi::TypeError::New(env, std::string(k) + " must be a number").ThrowAsJavaScriptException();
        return false;
    }
    dst = static_cast<T>(v.As<Napi::Number>().DoubleValue());
    return true;
}

bool ReadBool(Napi::Env env, Napi::Object o, const char* k, bool& dst) {
    if (!o.Has(k)) return true;
    Napi::Value v = o.Get(k);
    if (v.IsUndefined()) return true;
    if (!v.IsBoolean()) {
        Napi::TypeError::New(env, std::string(k) + " must be a boolean").ThrowAsJavaScriptException();
        return false;
    }
    dst = v.As<Napi::Boolean>().Value();
    return true;
}

}  // namespace

void JpegEncoder::Init(Napi::Env env, Napi::Object exports) {
    Napi::Function ctor = DefineClass(env, "JpegEncoder", {
        InstanceAccessor("options", &JpegEncoder::GetOptions, nullptr),
        InstanceMethod("getEncoder", &JpegEncoder::GetEncoder),
    });
    exports.Set("JpegEncoder", ctor);
}

JpegEncoder::JpegEncoder(const Napi::CallbackInfo& info) : Napi::ObjectWrap<JpegEncoder>(info) {
    Napi::Env env = info.Env();
    Napi::Object o = Napi::Object::New(env);
    if (info.Length() >= 1 && !info[0].IsUndefined()) {
        if (!info[0].IsObject()) {
            Napi::TypeError::New(env, "options must be an object").ThrowAsJavaScriptException();
            return;
        }
        o = info[0].As<Napi::Object>();
    }

    if (!(ReadNum(env, o, "thread", opts_.thread) &&
          ReadNum(env, o, "originalDimension", opts_.originalDimension) &&
          ReadNum(env, o, "bitPixel", opts_.bitPixel) &&
          ReadNum(env, o, "bitSize", opts_.bitSize) &&
          ReadBool(env, o, "extractTimeStamp", opts_.extractTimeStamp) &&
          ReadBool(env, o, "removeWatermark", opts_.removeWatermark)))
        return;

    if (opts_.thread < 1 || opts_.thread > 64 || opts_.originalDimension <= 0 ||
        opts_.bitPixel <= 0.0 || opts_.bitSize < 1 || opts_.bitSize > 32) {
        Napi::RangeError::New(env, "Invalid options: thread in [1,64], originalDimension>0, "
                                   "bitPixel>0, bitSize in [1,32]").ThrowAsJavaScriptException();
        return;
    }

    Napi::Object out = Napi::Object::New(env);
    out.Set("thread", opts_.thread);
    out.Set("originalDimension", opts_.originalDimension);
    out.Set("extractTimeStamp", opts_.extractTimeStamp);
    out.Set("removeWatermark", opts_.removeWatermark);
    out.Set("bitPixel", opts_.bitPixel);
    out.Set("bitSize", opts_.bitSize);
    optionsObj_ = Napi::Persistent(out);
}

Napi::Value JpegEncoder::GetOptions(const Napi::CallbackInfo&) { return optionsObj_.Value(); }

Napi::Value JpegEncoder::GetEncoder(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsObject()) {
        Napi::TypeError::New(env, "getEncoder expects a PixelBufferOptions object").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    Napi::Object o = info[0].As<Napi::Object>();

    FrameLayout lay;
    int yStride = 0;
    bool fullRange = false;
    if (!ReadNum(env, o, "width", lay.width) || !ReadNum(env, o, "height", lay.height) ||
        !ReadNum(env, o, "yStride", yStride) || !ReadNum(env, o, "bufferSize", lay.bufferSize) ||
        !ReadBool(env, o, "fullRange", fullRange))
        return env.Undefined();
    lay.fullRange = fullRange;

    Napi::Value fv = o.Get("format");
    std::string fmt = fv.IsString() ? fv.As<Napi::String>().Utf8Value() : "";
    if (fmt == "NV12")      lay.format = PixelFormat::NV12;
    else if (fmt == "I420") lay.format = PixelFormat::I420;
    else {
        Napi::TypeError::New(env, "format must be 'NV12' or 'I420'").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    if (lay.width <= 0 || lay.height <= 0 || (lay.width & 1) || (lay.height & 1)) {
        Napi::RangeError::New(env, "width and height must be positive even integers").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    if (yStride != 0 && (yStride < lay.width || (yStride & 1))) {
        Napi::RangeError::New(env, "yStride must be 0 or an even number >= width").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    lay.stride = yStride > 0 ? yStride : lay.width;

    // 找空闲编码器
    for (auto& ref : encoders_) {
        Napi::Object obj = ref.Value();
        auto* e = PixelBufferEncoder::Unwrap(obj);
        if (!e->InUse()) {
            e->Acquire(env, lay);
            return obj;
        }
    }

    // 没有空闲的：上限内新建
    if (static_cast<int>(encoders_.size()) >= opts_.thread) {
        Napi::Error::New(env, "No idle encoder available (all " + std::to_string(opts_.thread) +
                              " encoders are in use, call release() first)").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    auto* data = env.GetInstanceData<AddonData>();
    Napi::Object obj = data->encoderCtor.New({ Napi::External<EncoderOptions>::New(env, &opts_) });
    if (env.IsExceptionPending()) return env.Undefined();
    encoders_.push_back(Napi::Persistent(obj));
    PixelBufferEncoder::Unwrap(obj)->Acquire(env, lay);
    return obj;
}