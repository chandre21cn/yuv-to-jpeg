export interface JpegEncoderOptions {
    thread: number;
    originalDimension: number;
    extractTimeStamp: boolean;
    removeWatermark: boolean;
    bitPixel: number;
    bitSize: number;
}

export interface PixelBufferOptions {
    width: number;
    height: number;
    format: 'NV12' | 'I420';
    /** Y 平面 stride，0 或不传则用 width（若真实 stride > width 须配合 bufferSize） */
    yStride?: number;
    /** 缓冲区字节数，通常传 frame.allocationSize()，保证装得下带 stride 填充的整帧 */
    bufferSize?: number;
}

export interface PixelBufferEncoder {
    readonly width: number;
    readonly height: number;
    readonly data: Uint8Array;
    readonly timestamp?: number;
    /** 设置真实 stride（copyTo 后用 layout[0].stride 调用） */
    setStride(stride: number): void;
    toJpeg(quality: number): Promise<Uint8Array>;
    toBlob(quality: number): Promise<Blob>;
    release(): void;
}

export class JpegEncoder {
    constructor(options?: Partial<JpegEncoderOptions>);
    readonly options: JpegEncoderOptions;
    getEncoder(
        options: PixelBufferOptions & Partial<Pick<PixelBufferOptions, 'yStride' | 'bufferSize'>>
    ): PixelBufferEncoder;
}
