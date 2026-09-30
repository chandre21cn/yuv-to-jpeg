#include <napi.h>
#include <turbojpeg.h>
#include <libyuv.h>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>

#if defined(__x86_64__) || defined(_M_X64)
    #include <immintrin.h>
#elif defined(__ARM_NEON) || defined(__aarch64__)
    #include <arm_neon.h>
#endif

// ---------------------------------------------------------------------------
// 1. 全局静态查找表与线程缓存池
// ---------------------------------------------------------------------------
struct ColorLUT {
    uint8_t y[256], uv[256];
    ColorLUT() {
        for (int i = 0; i < 256; ++i) {
            y[i] = static_cast<uint8_t>(std::clamp((i - 16) * 255 / 219, 0, 255));
            uv[i] = static_cast<uint8_t>(std::clamp((i - 128) * 255 / 224 + 128, 0, 255));
        }
    }
} static const g_lut;

struct ThreadContext {
    tjhandle tj = nullptr;
    // 仅保留 NV12 -> I420 转换所需的 U/V 临时拆包平面缓存 (UV 分量仅为 Y 平面的 1/2 大小)
    std::vector<uint8_t> u, v; 
    ThreadContext() { tj = tj3Init(TJINIT_COMPRESS); }
    ~ThreadContext() { if (tj) tj3Destroy(tj); }
};
static thread_local ThreadContext t_ctx;

// 帧处理配置参数结构体
struct FrameOptions {
    int quality = 80;
    int originalWidth = 1080;
    bool removeWatermark = true;
    bool stretchColorRange = true;
    bool isI420 = false;
};

// 帧处理输入上下文结构体
struct InputContext {
    uint8_t* workPtr = nullptr;
    size_t length = 0;
    int width = 0;
    int height = 0;
    bool isValid = false;
};

// ---------------------------------------------------------------------------
// 2. 底层通用加速函数
// ---------------------------------------------------------------------------
// SIMD 极速求和 (针对时间戳检测)
static inline uint32_t FastSum(const uint8_t* ptr, int len) {
    uint32_t sum = 0; int x = 0;
#if defined(__ARM_NEON) || defined(__aarch64__)
    uint32x4_t vSum = vdupq_n_u32(0);
    for (; x <= len - 16; x += 16) {
        uint8x16_t data = vld1q_u8(ptr + x);
        vSum = vaddw_high_u16(vSum, vaddl_u8(vget_low_u8(data), vget_high_u8(data)));
    }
    sum += vaddvq_u32(vSum);
#elif defined(__x86_64__) || defined(_M_X64)
    __m256i vSum = _mm256_setzero_si256(), vZero = _mm256_setzero_si256();
    for (; x <= len - 32; x += 32) {
        vSum = _mm256_add_epi64(vSum, _mm256_sad_epu8(_mm256_loadu_si256((const __m256i*)(ptr + x)), vZero));
    }
    uint64_t mask[4]; _mm256_storeu_si256((__m256i*)mask, vSum);
    sum += (mask[0] + mask[1] + mask[2] + mask[3]);
#endif
    for (; x < len; ++x) sum += ptr[x];
    return sum;
}

// 查表法应用 (针对色彩拉伸)
static inline void ApplyLUT(uint8_t* ptr, int size, const uint8_t* lut) {
    int i = 0;
    for (; i <= size - 8; i += 8) {
        ptr[i] = lut[ptr[i]]; ptr[i+1] = lut[ptr[i+1]];
        ptr[i+2] = lut[ptr[i+2]]; ptr[i+3] = lut[ptr[i+3]];
        ptr[i+4] = lut[ptr[i+4]]; ptr[i+5] = lut[ptr[i+5]];
        ptr[i+6] = lut[ptr[i+6]]; ptr[i+7] = lut[ptr[i+7]];
    }
    for (; i < size; ++i) ptr[i] = lut[ptr[i]];
}

// ---------------------------------------------------------------------------
// 3. 独立业务功能与输入解析辅助函数
// ---------------------------------------------------------------------------

// [辅助 1] 解析 JavaScript 可选参数配置
static FrameOptions ParseOptions(const Napi::CallbackInfo& info) {
    FrameOptions opts;
    if (info.Length() >= 4 && info[3].IsObject()) {
        Napi::Object jsOpts = info[3].As<Napi::Object>();
        if (jsOpts.Has("quality")) opts.quality = jsOpts.Get("quality").As<Napi::Number>().Int32Value();
        if (jsOpts.Has("originalWidth")) opts.originalWidth = jsOpts.Get("originalWidth").As<Napi::Number>().Int32Value();
        if (jsOpts.Has("removeWatermark")) opts.removeWatermark = jsOpts.Get("removeWatermark").As<Napi::Boolean>().Value();
        if (jsOpts.Has("stretchColorRange")) opts.stretchColorRange = jsOpts.Get("stretchColorRange").As<Napi::Boolean>().Value();
        if (jsOpts.Has("format")) {
            std::string fmt = jsOpts.Get("format").As<Napi::String>().Utf8Value();
            opts.isI420 = (fmt == "I420");
        }
    }
    return opts;
}

// [辅助 2] 校验并提取输入 Buffer 与基础尺寸参数（零拷贝直接引用 JS 内存空间）
static InputContext ValidateAndExtractInput(const Napi::CallbackInfo& info) {
    InputContext ctx;
    if (info.Length() < 3 || !info[0].IsBuffer() || !info[1].IsNumber() || !info[2].IsNumber()) return ctx;

    Napi::Buffer<uint8_t> buffer = info[0].As<Napi::Buffer<uint8_t>>();
    ctx.width = info[1].As<Napi::Number>().Int32Value();
    ctx.height = info[2].As<Napi::Number>().Int32Value();
    ctx.length = buffer.Length();

    if (ctx.width <= 0 || ctx.height <= 0 || ctx.length < static_cast<size_t>(ctx.width * ctx.height * 3 / 2)) {
        return ctx;
    }

    // 零拷贝关键点：直接使用 Buffer 数据指针（In-place 操作）
    ctx.workPtr = buffer.Data();
    ctx.isValid = true;
    return ctx;
}

// [辅助 3] 计算缩放因子
static inline double CalculateScale(int w, int h, int originalWidth) {
    int targetBase = originalWidth > 0 ? originalWidth : 1280;
    return (w < h) ? (static_cast<double>(h) / targetBase)
                   : (static_cast<double>(w) / targetBase);
}

// [功能 1] 提取时间戳
static uint32_t ExtractTimeStamp(const uint8_t* buf, int w, int h, double scale) {
    int cbW = static_cast<int>(std::floor(4.0 * scale));
    int cbH = static_cast<int>(std::floor(4.0 * scale));
    if (w < 128 * scale || h < 8 * scale || cbW <= 0 || cbH <= 0) return 0;

    size_t bottomBase = static_cast<size_t>(cbH) * w;
    uint32_t val = 0;

    for (int bit = 0; bit < 32; bit++) {
        int xStart = static_cast<int>(std::floor(bit * 4.0 * scale));
        uint32_t top = 0, bot = 0;
        for (int y = 0; y < cbH; y++) {
            size_t row = static_cast<size_t>(y) * w + xStart;
            top += FastSum(buf + row, cbW);
            bot += FastSum(buf + bottomBase + row, cbW);
        }
        if (top > bot) val |= (1U << bit);
    }
    return val;
}

// [功能 2] 水印覆盖抹除 (In-place 局部修剪，零全局内存移动)
static void RemoveWatermark(uint8_t* buf, int w, int h, double scale, bool isI420) {
    int wmW = std::min(w, static_cast<int>(std::ceil(128.0 * scale) + 2));
    int wmH = std::min(h, static_cast<int>(std::ceil(8.0 * scale) + 2));
    if (wmW <= 0 || wmH <= 0) return;

    size_t ySize = static_cast<size_t>(w) * h;
    int sampleY = wmH + 2;
    if (sampleY < h) {
        const uint8_t* src = buf + (static_cast<size_t>(sampleY) * w);
        for (int r = 0; r < wmH; r++) std::memmove(buf + (static_cast<size_t>(r) * w), src, wmW);
    }

    int sampleChroma = (wmH / 2) + 1;
    if (sampleChroma < (h / 2)) {
        if (isI420) {
            int stride = w / 2, cW = wmW / 2, cH = wmH / 2;
            uint8_t *u = buf + ySize, *v = u + (ySize / 4);
            const uint8_t *uSrc = u + (static_cast<size_t>(sampleChroma) * stride);
            const uint8_t *vSrc = v + (static_cast<size_t>(sampleChroma) * stride);
            for (int r = 0; r < cH; r++) {
                std::memmove(u + (static_cast<size_t>(r) * stride), uSrc, cW);
                std::memmove(v + (static_cast<size_t>(r) * stride), vSrc, cW);
            }
        } else {
            uint8_t* uv = buf + ySize;
            const uint8_t* uvSrc = uv + (static_cast<size_t>(sampleChroma) * w);
            for (int r = 0; r < (wmH / 2); r++) std::memmove(uv + (static_cast<size_t>(r) * w), uvSrc, wmW);
        }
    }
}

// [功能 3] 色彩格式解包与色彩范围调整 (NV12 -> I420 + LUT)
static void PrepareI420Planes(
    uint8_t* workingPtr, int w, int h, bool isI420, bool stretch,
    uint8_t*& yPlane, const uint8_t*& uPlane, const uint8_t*& vPlane
) {
    int ySize = w * h, uvW = w / 2, uvH = h / 2, cSize = uvW * uvH;
    yPlane = workingPtr;

    if (isI420) {
        uPlane = workingPtr + ySize;
        vPlane = workingPtr + ySize + cSize;
    } else {
        if (t_ctx.u.size() < static_cast<size_t>(cSize)) {
            t_ctx.u.resize(cSize); t_ctx.v.resize(cSize);
        }
        libyuv::NV12ToI420(workingPtr, w, workingPtr + ySize, w, yPlane, w, t_ctx.u.data(), uvW, t_ctx.v.data(), uvW, w, h);
        uPlane = t_ctx.u.data();
        vPlane = t_ctx.v.data();
    }

    if (stretch) {
        ApplyLUT(yPlane, ySize, g_lut.y);
        ApplyLUT(const_cast<uint8_t*>(uPlane), cSize, g_lut.uv);
        ApplyLUT(const_cast<uint8_t*>(vPlane), cSize, g_lut.uv);
    }
}

// [功能 4] TurboJPEG 编码压缩 (彻底解决 External buffer 报错)
static Napi::Buffer<uint8_t> CompressYuvToJpeg(
    Napi::Env env, const uint8_t* y, const uint8_t* u, const uint8_t* v,
    int w, int h, int quality
) {
    if (!t_ctx.tj) t_ctx.tj = tj3Init(TJINIT_COMPRESS);

    // 极致参数设置
    tj3Set(t_ctx.tj, TJPARAM_QUALITY, quality);
    tj3Set(t_ctx.tj, TJPARAM_SUBSAMP, TJSAMP_420);
    tj3Set(t_ctx.tj, TJPARAM_FASTDCT, 1);
    tj3Set(t_ctx.tj, TJPARAM_FASTUPSAMPLE, 1);
    tj3Set(t_ctx.tj, TJPARAM_NOREALLOC, 1);

    size_t maxJpegSize = tjBufSize(w, h, TJSAMP_420);
    if (maxJpegSize == 0) return Napi::Buffer<uint8_t>::New(env, 0);

    // ✅ 关键改动：直接由 Node.js 创建合法 Buffer 内存块
    Napi::Buffer<uint8_t> outBuffer = Napi::Buffer<uint8_t>::New(env, maxJpegSize);

    const uint8_t* planes[3] = { y, u, v };
    int strides[3] = { w, w / 2, w / 2 };
    unsigned char* jpegBuf = outBuffer.Data();
    size_t actualJpegSize = maxJpegSize;

    // 直接编码写入 Node.js 的 Buffer 指针
    if (tj3CompressFromYUVPlanes8(t_ctx.tj, planes, w, strides, h, &jpegBuf, &actualJpegSize) != 0 || actualJpegSize == 0) {
        return Napi::Buffer<uint8_t>::New(env, 0);
    }

    // ✅ 裁剪实际大小并返回，彻底规避 External buffer 拦截限制
    return Napi::Buffer<uint8_t>::Copy(env, outBuffer.Data(), actualJpegSize);
}

// ---------------------------------------------------------------------------
// 4. NAPI 导出入口 (高层流水线编排)
// ---------------------------------------------------------------------------
Napi::Value ProcessFrame(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    // 0. 解析配置选项与提取输入
    FrameOptions opts = ParseOptions(info);
    InputContext input = ValidateAndExtractInput(info);
    if (!input.isValid) return env.Null();

    double scale = CalculateScale(input.width, input.height, opts.originalWidth);

    // 1. 提取时间戳
    uint32_t timestamp = ExtractTimeStamp(input.workPtr, input.width, input.height, scale);

    // 2. 抹除水印 (In-place 直接覆盖)
    if (opts.removeWatermark) {
        RemoveWatermark(input.workPtr, input.width, input.height, scale, opts.isI420);
    }

    // 3. 格式拆包 & 动态色彩拉伸
    uint8_t* yPlane = nullptr;
    const uint8_t *uPlane = nullptr, *vPlane = nullptr;
    PrepareI420Planes(input.workPtr, input.width, input.height, opts.isI420, opts.stretchColorRange, yPlane, uPlane, vPlane);

    // 4. TurboJPEG 编码压缩
    Napi::Buffer<uint8_t> jpegBuffer = CompressYuvToJpeg(env, yPlane, uPlane, vPlane, input.width, input.height, opts.quality);

    // 5. 构造并返回结果
    Napi::Object res = Napi::Object::New(env);
    res.Set("timestamp", Napi::Number::New(env, timestamp));
    res.Set("jpegBuffer", jpegBuffer);

    return res;
}

Napi::Object Init(Napi::Env env, Napi::Object exports) {
    exports.Set(Napi::String::New(env, "processFrame"), Napi::Function::New(env, ProcessFrame));
    return exports;
}

NODE_API_MODULE(yuv2jpeg_worker, Init)