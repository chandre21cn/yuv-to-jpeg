const path = require('path');
const load = require('node-gyp-build');

// 缓存加载后的原生模块（单例）
let cachedAddon = null;

/**
 * 使用 node-gyp-build 自动寻找并加载 native addon 模块
 */
function loadNativeAddon() {
    if (!cachedAddon) {
        cachedAddon = load(__dirname);
    }
    return cachedAddon;
}

/**
 * @typedef {Object} ProcessFrameOptions
 * @property {number} [quality=80] - JPEG 压缩质量 (1-100)
 * @property {number} [originalWidth=1080] - 计算缩放比例的原图基准宽度
 * @property {boolean} [removeWatermark=true] - 是否擦除左上角水印
 * @property {'NV12' | 'I420'} [format='NV12'] - 像素格式，支持 'NV12' 或 'I420'
 * @property {boolean} [asBlob=false] - 是否生成并返回 Blob 对象
 */

/**
 * @typedef {Object} ProcessFrameResult
 * @property {number} timestamp - 提取出的 32 位时间戳
 * @property {Buffer} jpegBuffer - JPEG Buffer 数据
 * @property {Blob} [jpegBlob] - 生成的 JPEG Blob 对象（当 asBlob = true 时存在）
 */

/**
 * 集中处理 YUV 视频帧：提取 32 位时间戳、去水印并转换为 JPEG Buffer / Blob
 * 
 * @param {Buffer} nv12Buffer - YUV 格式内存数据 (支持 NV12 或 I420)
 * @param {number} width - 视频帧宽度 (codedWidth)
 * @param {number} height - 视频帧高度 (codedHeight)
 * @param {ProcessFrameOptions} [options] - 配置参数
 * @returns {Promise<ProcessFrameResult>} 包含提取出的时间戳与 JPEG Buffer / Blob
 */
async function processFrame(nv12Buffer, width, height, options = {}) {
    const opts = {
        quality: 80,
        originalWidth: 1080,
        removeWatermark: true,
        format: 'NV12',
        asBlob: false,
        ...options
    };

    const nativeAddon = loadNativeAddon();
    const result = await nativeAddon.processFrame(nv12Buffer, width, height, opts);

    if (opts.asBlob) {
        // Node.js Buffer 是 Uint8Array 的子类，可直接转为 Blob
        result.jpegBlob = new Blob([result.jpegBuffer], { type: 'image/jpeg' });
    }

    return result;
}

/**
 * 仅将 NV12 转换成 JPEG Buffer（兼容旧接口）
 * 
 * @param {Buffer} nv12Buffer - NV12 格式内存数据
 * @param {number} width - 图片宽度
 * @param {number} height - 图片高度
 * @param {number} [quality=80] - JPEG 压缩质量 (1-100)
 * @returns {Promise<Buffer>} 返回包含 JPEG 数据的 Promise
 */
async function convertNV12ToJpeg(nv12Buffer, width, height, quality = 80) {
    const result = await processFrame(nv12Buffer, width, height, {
        quality,
        removeWatermark: false,
        format: 'NV12',
        asBlob: false
    });
    return result.jpegBuffer;
}

module.exports = {
    processFrame,
    convertNV12ToJpeg
};