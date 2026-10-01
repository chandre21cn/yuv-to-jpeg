#pragma once
#include "common.h"
#include <napi.h>
#include <vector>

class JpegEncoder : public Napi::ObjectWrap<JpegEncoder> {
public:
    static void Init(Napi::Env env, Napi::Object exports);
    explicit JpegEncoder(const Napi::CallbackInfo& info);

private:
    Napi::Value GetOptions(const Napi::CallbackInfo& info);
    Napi::Value GetEncoder(const Napi::CallbackInfo& info);

    EncoderOptions opts_;
    Napi::ObjectReference optionsObj_;
    std::vector<Napi::ObjectReference> encoders_;   // 最多 opts_.thread 个
};