#include "addon_data.h"
#include "jpeg_encoder.h"
#include "pixel_buffer_encoder.h"

Napi::Object Init(Napi::Env env, Napi::Object exports) {
    auto* data = new AddonData();
    data->encoderCtor = Napi::Persistent(PixelBufferEncoder::Define(env));
    env.SetInstanceData(data);
    JpegEncoder::Init(env, exports);
    return exports;
}

NODE_API_MODULE(yuv2jpeg_worker, Init)