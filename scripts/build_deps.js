const { execSync } = require('child_process');
const fs = require('fs');
const path = require('path');

// 依赖安装在项目根目录下的 deps 文件夹中，与 binding.gyp 的默认 turbojpeg_root 保持一致：
//   <(module_root_dir)/deps/libjpeg-turbo/{include,lib}
const projectRootDir = path.resolve(__dirname, '..');
const depsDir = path.join(projectRootDir, 'deps');

// 目标架构：优先用 npm_config_arch（交叉编译时由 node-gyp 设置），否则用当前架构
const targetArch = process.env.npm_config_arch || process.arch;
const markerFile = path.join(depsDir, `.built.${process.platform}.${targetArch}.marker`);

// 已经编译过则跳过
if (fs.existsSync(markerFile)) {
  console.log(`[build_deps] Dependencies already built for ${process.platform}-${targetArch}.`);
  process.exit(0);
}

fs.mkdirSync(depsDir, { recursive: true });

function run(cmd, cwd) {
  console.log(`[build_deps] $ ${cmd}`);
  execSync(cmd, { stdio: 'inherit', cwd: cwd || projectRootDir });
}

// src 中仅使用 TurboJPEG（<turbojpeg.h>）做 JPEG 压缩；
// NV12→U/V 平面拆分已由 src/yuv_utils.h 的 SplitUVPlane 自行实现，不再依赖 libyuv。
function buildLibJpegTurbo() {
  const prefix = path.join(depsDir, 'libjpeg-turbo');
  const srcDir = path.join(prefix, 'src');
  const buildDir = path.join(prefix, 'build');

  if (!fs.existsSync(srcDir)) {
    run(`git clone --depth 1 --branch 3.2.0 https://github.com/libjpeg-turbo/libjpeg-turbo.git "${srcDir}"`);
  }

  fs.mkdirSync(buildDir, { recursive: true });

  // libjpeg-turbo 的 SIMD 优化在 x86/x64 上依赖 NASM，ARM64 上使用 NEON。
  // 若未安装 NASM（仅 x64 需要），降级为标量模式（WITH_SIMD=OFF），虽然慢但能正常工作。
  let withSimd = 'ON';
  if (targetArch === 'x64') {
    try {
      execSync('nasm --version', { stdio: 'pipe' });
    } catch {
      console.warn('[build_deps] WARNING: NASM not found. libjpeg-turbo will be built WITHOUT SIMD (slower).');
      console.warn('[build_deps]          Install NASM for optimal performance:');
      console.warn('[build_deps]          Windows: choco install nasm  (or https://nasm.us)');
      console.warn('[build_deps]          macOS:   brew install nasm');
      console.warn('[build_deps]          Linux:   sudo apt install nasm');
      withSimd = 'OFF';
    }
  }

  // Windows 交叉编译：指定 cmake 目标架构
  const cmakeArch = process.platform === 'win32'
    ? (targetArch === 'arm64' ? 'ARM64' : 'x64')
    : null;
  const archFlag = cmakeArch ? `-A ${cmakeArch}` : '';

  // 本项目只用到 TurboJPEG 的压缩 API，不需要工具/测试/算术编解码。
  // 关闭这些功能以减小静态库体积。
  // 加 -ffunction-sections 让链接器能做函数级 dead code elimination，
  // 把 TurboJPEG 中未引用的解压/转换代码从最终 .node 中剥离。
  const trimCFlags = process.platform === 'win32'
    ? '/utf-8 /Gy'
    : '-ffunction-sections -fdata-sections -fvisibility=hidden';

  const winFlags = process.platform === 'win32'
    ? `-DCMAKE_CXX_FLAGS="${trimCFlags}" -DCMAKE_C_FLAGS="${trimCFlags}"`
    : '';
  const macFlags = process.platform === 'darwin'
    ? `-DCMAKE_OSX_DEPLOYMENT_TARGET=10.15 -DCMAKE_C_FLAGS="${trimCFlags}" -DCMAKE_CXX_FLAGS="${trimCFlags}"`
    : '';

  run(
    `cmake -S "${srcDir}" -B "${buildDir}" ${archFlag} ` +
    `-DCMAKE_INSTALL_PREFIX="${prefix}" ` +
    `-DENABLE_SHARED=OFF -DENABLE_STATIC=ON -DWITH_TURBOJPEG=ON ` +
    `-DWITH_SIMD=${withSimd} ` +
    `-DWITH_JPEG7=OFF -DWITH_JPEG8=OFF ` +
    `-DWITH_ARITH_ENC=OFF -DWITH_ARITH_DEC=OFF ` +
    `-DWITH_TOOLS=OFF -DWITH_TESTS=OFF -DWITH_FUZZ=OFF ` +
    `-DWITH_JAVA=OFF -DWITH_TURBOJPEG_JNI=OFF ` +
    `-DCMAKE_BUILD_TYPE=Release ${winFlags} ${macFlags}`
  );
  run(`cmake --build "${buildDir}" --config Release --target install`);
}

try {
  buildLibJpegTurbo();
  fs.writeFileSync(markerFile, 'done');
  console.log('[build_deps] Dependencies built successfully.');
} catch (error) {
  console.error('[build_deps] Failed to build dependencies:', error);
  process.exit(1);
}
