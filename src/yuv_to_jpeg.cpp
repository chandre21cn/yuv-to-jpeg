#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <napi.h>
#include <uv.h>
#include <turbojpeg.h>
#include <libyuv.h>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <memory>

#if defined(__x86_64__) || defined(_M_X64)
    #include <immintrin.h>
#elif defined(__ARM_NEON) || defined(__aarch64__)
    #include <arm_neon.h>
#endif

#define EVEN_FLOOR(x) ((x) & ~1)

// ---------------------------------------------------------------------------
// 1. Global Static Look-Up Tables
// ---------------------------------------------------------------------------
struct ColorLUT {
    uint8_t y[256], uv[256];
    ColorLUT() {
        for (int i = 0; i < 256; ++i) {
            y[i] = static_cast<uint8_t>((std::clamp)((i - 16) * 255 / 219, 0, 255));
            uv[i] = static_cast<uint8_t>((std::clamp)((i - 128) * 255 / 224 + 128, 0, 255));
        }
    }
} static const g_lut;

// Options configuration structure
struct FrameOptions {
    int quality = 80;
    int originalWidth = 1080;
    bool removeWatermark = true;
    bool stretchColorRange = true;
    bool isI420 = false;
};

// ---------------------------------------------------------------------------
// 2. High-Performance SIMD Acceleration
// ---------------------------------------------------------------------------
static inline uint32_t FastSum(const uint8_t* ptr, int len) {
    if (len <= 0) return 0;
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
// 3. Image Processing Helpers
// ---------------------------------------------------------------------------
static inline double CalculateScale(int w, int h, int originalWidth) {
    int targetBase = originalWidth > 0 ? originalWidth : 1080;
    int maxDim = (std::max)(w, h);
    return static_cast<double>(maxDim) / static_cast<double>(targetBase);
}

static uint32_t ExtractTimeStamp(const uint8_t* buf, int w, int h, double scale) {
    int cbW = static_cast<int>(std::floor(4.0 * scale));
    int cbH = static_cast<int>(std::floor(4.0 * scale));
    if (w < static_cast<int>(128 * scale) || h < static_cast<int>(8 * scale) || cbW <= 0 || cbH <= 0) return 0;

    size_t bottomBase = static_cast<size_t>(cbH) * w;
    uint32_t val = 0;

    for (int bit = 0; bit < 32; bit++) {
        int xStart = static_cast<int>(std::floor(bit * 4.0 * scale));
        if (xStart + cbW > w || cbH >= h) break;

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

static void RemoveWatermark(uint8_t* buf, int w, int h, double scale, bool isI420) {
    int wmW = (std::min)(w, EVEN_FLOOR(static_cast<int>(std::ceil(128.0 * scale) + 2)));
    int wmH = (std::min)(h, EVEN_FLOOR(static_cast<int>(std::ceil(8.0 * scale) + 2)));
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

// ---------------------------------------------------------------------------
// 4. Native Context-Safe Task (Zero-Copy)
// ---------------------------------------------------------------------------
struct WorkerTask {
    uv_work_t req;
    Napi::Env env;
    Napi::Promise::Deferred deferred;
    std::shared_ptr<bool> envLifetime; // 环境生命周期标志

    Napi::Reference<Napi::Buffer<uint8_t>> bufferRef; // 锁住 JS Buffer，避免 GC 提前回收
    uint8_t* inputData = nullptr;                     // 零拷贝指针
    size_t dataSize = 0;

    int width = 0;
    int height = 0;
    FrameOptions opts;

    std::string errorMessage;
    uint32_t timestamp = 0;
    std::vector<uint8_t> compressedJpeg;

    WorkerTask(Napi::Env e, Napi::Buffer<uint8_t> buf, int w, int h, FrameOptions o, std::shared_ptr<bool> lifetime)
        : env(e), deferred(Napi::Promise::Deferred::New(e)), envLifetime(lifetime), width(w), height(h), opts(o) {
        bufferRef = Napi::Persistent(buf); // 强引用，阻止 GC 销毁
        inputData = buf.Data();            // 零拷贝获取底层内存
        dataSize = buf.Length();
        req.data = this;
    }
};

// 后台线程池计算逻辑
static void ExecuteTask(uv_work_t* req) {
    WorkerTask* task = static_cast<WorkerTask*>(req->data);
    if (!task->inputData || task->dataSize == 0) {
        task->errorMessage = "Input buffer data is null or empty";
        return;
    }

    // 使用本地副本，防止写操作损坏 JS 侧的主线程 Buffer 数据或造成跨线程竞争
    std::vector<uint8_t> localBuffer(task->inputData, task->inputData + task->dataSize);
    uint8_t* dataPtr = localBuffer.data();

    double scale = CalculateScale(task->width, task->height, task->opts.originalWidth);

    // 1. 提取时间戳
    task->timestamp = ExtractTimeStamp(dataPtr, task->width, task->height, scale);

    // 2. 去水印处理
    if (task->opts.removeWatermark) {
        RemoveWatermark(dataPtr, task->width, task->height, scale, task->opts.isI420);
    }

    // 3. 平面分离与颜色拉伸
    int ySize = task->width * task->height;
    int uvW = task->width / 2;
    int uvH = task->height / 2;
    int cSize = uvW * uvH;

    uint8_t* yPlane = dataPtr;
    uint8_t* uPlane = nullptr;
    uint8_t* vPlane = nullptr;

    std::vector<uint8_t> uBuf, vBuf;

    if (task->opts.isI420) {
        uPlane = dataPtr + ySize;
        vPlane = dataPtr + ySize + cSize;
    } else {
        uBuf.resize(cSize);
        vBuf.resize(cSize);
        libyuv::NV12ToI420(
            dataPtr, task->width, 
            dataPtr + ySize, task->width, 
            yPlane, task->width, 
            uBuf.data(), uvW, 
            vBuf.data(), uvW, 
            task->width, task->height
        );
        uPlane = uBuf.data();
        vPlane = vBuf.data();
    }

    if (task->opts.stretchColorRange) {
        ApplyLUT(yPlane, ySize, g_lut.y);
        ApplyLUT(uPlane, cSize, g_lut.uv);
        ApplyLUT(vPlane, cSize, g_lut.uv);
    }

    // 4. TurboJPEG 编码压缩
    tjhandle tj = tjInitCompress();
    if (!tj) {
        task->errorMessage = "Failed to initialize TurboJPEG compressor instance";
        return;
    }

    unsigned char* jpegBuf = nullptr;
    unsigned long jpegSize = 0;
    const uint8_t* planes[3] = { yPlane, uPlane, vPlane };
    int strides[3] = { task->width, task->width / 2, task->width / 2 };

    int flags = TJFLAG_FASTDCT;

    if (tjCompressFromYUVPlanes(tj, planes, task->width, strides, task->height, TJSAMP_420, &jpegBuf, &jpegSize, task->opts.quality, flags) != 0) {
        std::string errStr = tjGetErrorStr2(tj);
        if (jpegBuf) tjFree(jpegBuf);
        tjDestroy(tj);
        task->errorMessage = "TurboJPEG Compression Error: " + errStr;
        return;
    }

    task->compressedJpeg.assign(jpegBuf, jpegBuf + jpegSize);
    tjFree(jpegBuf);
    tjDestroy(tj);
}

// 主线程任务回调
static void CompleteTask(uv_work_t* req, int status) {
    WorkerTask* task = static_cast<WorkerTask*>(req->data);

    // 检查 Node 上下文是否在执行过程中已销毁/刷新
    bool isAlive = task->envLifetime && (*(task->envLifetime)) && (status != UV_ECANCELED);

    if (isAlive) {
        Napi::Env env = task->env;
        napi_handle_scope scope;
        if (napi_open_handle_scope(env, &scope) == napi_ok) {
            try {
                if (!task->errorMessage.empty()) {
                    task->deferred.Reject(Napi::String::New(env, task->errorMessage));
                } else {
                    Napi::Buffer<uint8_t> resultJpegBuffer = Napi::Buffer<uint8_t>::Copy(env, task->compressedJpeg.data(), task->compressedJpeg.size());
                    Napi::Object res = Napi::Object::New(env);
                    res.Set("timestamp", Napi::Number::New(env, task->timestamp));
                    res.Set("jpegBuffer", resultJpegBuffer);

                    task->deferred.Resolve(res);
                }
            } catch (...) {}
            napi_close_handle_scope(env, scope);
        }
        // 正常情况下释放 Buffer 引用
        task->bufferRef.Reset();
    } else {
        // 如果环境已被注销，不调用 napi API，防止访问已销毁的环境触发致命错误
    }

    delete task;
}

// ---------------------------------------------------------------------------
// 5. Context Lifetime Registration
// ---------------------------------------------------------------------------
static void OnEnvCleanup(void* arg) {
    auto lifetime = static_cast<std::shared_ptr<bool>*>(arg);
    if (lifetime && *lifetime) {
        **lifetime = false; // 页面刷新，环境失效
    }
}

// ---------------------------------------------------------------------------
// 6. Options Parsing Helper
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// 7. Main JS Entrance
// ---------------------------------------------------------------------------
Napi::Value ProcessFrame(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    // 绑定/维护环境生命周期 Flag
    auto lifetimePtr = env.GetInstanceData<std::shared_ptr<bool>>();
    if (!lifetimePtr) {
        lifetimePtr = new std::shared_ptr<bool>(std::make_shared<bool>(true));
        env.SetInstanceData(lifetimePtr);
        napi_add_env_cleanup_hook(env, OnEnvCleanup, lifetimePtr);
    }

    if (info.Length() < 3) {
        Napi::Promise::Deferred deferred = Napi::Promise::Deferred::New(env);
        deferred.Reject(Napi::String::New(env, "Invalid arguments count: Expected at least 3 arguments (buffer, width, height)"));
        return deferred.Promise();
    }

    if (!info[0].IsBuffer() || !info[1].IsNumber() || !info[2].IsNumber()) {
        Napi::Promise::Deferred deferred = Napi::Promise::Deferred::New(env);
        deferred.Reject(Napi::String::New(env, "Invalid argument types: Expected (Buffer, Number, Number)"));
        return deferred.Promise();
    }

    Napi::Buffer<uint8_t> buffer = info[0].As<Napi::Buffer<uint8_t>>();
    int width = info[1].As<Napi::Number>().Int32Value();
    int height = info[2].As<Napi::Number>().Int32Value();

    if (width <= 0 || height <= 0) {
        Napi::Promise::Deferred deferred = Napi::Promise::Deferred::New(env);
        deferred.Reject(Napi::String::New(env, "Invalid dimensions: width and height must be positive integers."));
        return deferred.Promise();
    }

    width = EVEN_FLOOR(width);

    size_t bufferLen = buffer.Length();
    size_t maxPossiblePixels = bufferLen * 2 / 3;
    int safeHeight = static_cast<int>(maxPossiblePixels / width);
    safeHeight = EVEN_FLOOR(safeHeight);

    if (safeHeight < height) {
        height = safeHeight;
    }

    if (height < 2) {
        Napi::Promise::Deferred deferred = Napi::Promise::Deferred::New(env);
        deferred.Reject(Napi::String::New(env, "Buffer too small: Cannot even fit 2 rows for YUV420. Buffer length=" + std::to_string(bufferLen)));
        return deferred.Promise();
    }

    FrameOptions opts = ParseOptions(info);

    // 创建零拷贝任务
    WorkerTask* task = new WorkerTask(env, buffer, width, height, opts, *lifetimePtr);

    Napi::Promise promise = task->deferred.Promise();

    uv_loop_s* loop = nullptr;
    napi_get_uv_event_loop(env, &loop);
    uv_queue_work(loop, &task->req, ExecuteTask, CompleteTask);

    return promise;
}

Napi::Object Init(Napi::Env env, Napi::Object exports) {
    exports.Set(Napi::String::New(env, "processFrame"), Napi::Function::New(env, ProcessFrame));
    return exports;
}

NODE_API_MODULE(yuv2jpeg_worker, Init)