export interface ProcessFrameOptions {
    /**
     * JPEG 压缩质量 (1-100)
     * @default 80
     */
    quality?: number;
    /**
     * 计算缩放比例的原图基准宽度
     * @default 1080
     */
    originalWidth?: number;
    /**
     * 是否擦除左上角水印
     * @default true
     */
    removeWatermark?: boolean;
    /**
     * 像素格式，支持 NV12 或 I420
     * @default 'NV12'
     */
    format?: 'NV12' | 'I420';
    /**
     * 是否生成并返回 Blob 对象
     * @default false
     */
    asBlob?: boolean;
}

export interface ProcessFrameResult {
    /** 提取出的 32 位时间戳 */
    timestamp: number;
    /** 转换生成的 JPEG 内存 Buffer */
    jpegBuffer: Buffer;
    /** 当 options.asBlob 为 true 时生成的 JPEG Blob 对象 */
    jpegBlob?: Blob;
}

/**
 * 集中处理 YUV 视频帧：提取 32 位时间戳、去水印并转换为 JPEG (Buffer 或 Blob)
 * 
 * @param yuvBuffer - YUV 格式内存数据 (NV12 或 I420)
 * @param width - 视频帧宽度 (codedWidth)
 * @param height - 视频帧高度 (codedHeight)
 * @param options - 配置参数
 * @returns 包含提取出的时间戳与 JPEG 数据的 Promise
 */
export function processFrame(
    yuvBuffer: Buffer,
    width: number,
    height: number,
    options?: ProcessFrameOptions
): Promise<ProcessFrameResult>;

/**
 * 仅将 NV12 转换成 JPEG Buffer（兼容旧接口）
 * 
 * @param nv12Buffer - NV12 格式内存数据
 * @param width - 图片宽度
 * @param height - 图片高度
 * @param quality - JPEG 压缩质量 (1-100)
 * @returns 返回包含 JPEG 数据的 Promise
 */
export function convertNV12ToJpeg(
    nv12Buffer: Buffer,
    width: number,
    height: number,
    quality?: number
): Promise<Buffer>;