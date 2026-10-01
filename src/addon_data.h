#pragma once
#include <napi.h>

struct AddonData {
    Napi::FunctionReference encoderCtor;   // PixelBufferEncoder 构造器（不对外导出）
};