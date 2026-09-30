const { execSync } = require('child_process');
const fs = require('fs');
const path = require('path');

// 获取预编译目标架构 (如 x64, arm64)
const targetArch = process.env.npm_config_arch || process.env.ARCH || process.arch;

// 将依赖库安装在项目根目录下的 external_deps 文件夹中
const projectRootDir = path.resolve(__dirname, '..');
const externalDir = path.join(projectRootDir, 'external_deps');
// 标记文件增加架构区分，避免跨架构重复编译时直接跳过
const markerFile = path.join(externalDir, `external_built_${targetArch}.marker`);

// 如果当前架构已经编译过，则跳过
if (fs.existsSync(markerFile)) {
  console.log(`[build_deps] External dependencies for ${targetArch} already built.`);
  process.exit(0);
}

fs.mkdirSync(externalDir, { recursive: true });

function runCommand(cmd, cwd) {
  console.log(`[build_deps] Running: ${cmd}`);
  execSync(cmd, { stdio: 'inherit', cwd: cwd || process.cwd() });
}

// 获取平台专有的 CMake 配置参数
function getPlatformCMakeFlags() {
  if (process.platform !== 'win32') return '';

  let archFlag = '';
  if (targetArch === 'arm64') {
    archFlag = '-A ARM64';
  } else if (targetArch === 'x64') {
    archFlag = '-A x64';
  }

  // 1. 指定 -A 参数实现正确架构的交叉编译
  // 2. 指定 -DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreadedDLL" (/MD) 解决 __imp_fgets 及 LNK4098 运行时冲突
  return `${archFlag} -DCMAKE_CXX_FLAGS="/utf-8" -DCMAKE_C_FLAGS="/utf-8" -DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreadedDLL"`;
}

function buildLibTurboJpeg() {
  const prefix = path.join(externalDir, 'libjpeg-turbo');
  const srcDir = path.join(prefix, 'src');
  const buildDir = path.join(prefix, `build_${targetArch}`);
  const installDir = path.join(prefix, 'install');

  if (!fs.existsSync(srcDir)) {
    runCommand(`git clone --depth 1 --branch 3.2.0 https://github.com/libjpeg-turbo/libjpeg-turbo.git "${srcDir}"`);
  }

  fs.mkdirSync(buildDir, { recursive: true });

  const extraArgs = getPlatformCMakeFlags();
  const cmakeCmd = `cmake -S "${srcDir}" -B "${buildDir}" ` +
    `-DCMAKE_INSTALL_PREFIX="${installDir}" ` +
    `-DENABLE_SHARED=OFF -DENABLE_STATIC=ON -DWITH_TURBOJPEG=ON ` +
    `-DWITH_JAVA=OFF -DWITH_ARITH_ENC=OFF -DWITH_ARITH_DEC=OFF ` +
    `-DCMAKE_BUILD_TYPE=Release ${extraArgs}`;

  runCommand(cmakeCmd);
  runCommand(`cmake --build "${buildDir}" --config Release --target install`);
}

function buildLibYuv() {
  const prefix = path.join(externalDir, 'libyuv');
  const srcDir = path.join(prefix, 'src');
  const buildDir = path.join(prefix, `build_${targetArch}`);
  const installDir = path.join(prefix, 'install');

  if (!fs.existsSync(srcDir)) {
    runCommand(`git clone --depth 1 https://chromium.googlesource.com/libyuv/libyuv "${srcDir}"`);
  }

  fs.mkdirSync(buildDir, { recursive: true });

  const extraArgs = getPlatformCMakeFlags();
  const cmakeCmd = `cmake -S "${srcDir}" -B "${buildDir}" ` +
    `-DCMAKE_INSTALL_PREFIX="${installDir}" ` +
    `-DCMAKE_BUILD_TYPE=Release -DUNIT_TEST=OFF ${extraArgs}`;

  runCommand(cmakeCmd);
  runCommand(`cmake --build "${buildDir}" --config Release --target install`);
}

try {
  console.log(`[build_deps] Building external dependencies for target architecture: ${targetArch}`);
  buildLibTurboJpeg();
  buildLibYuv();
  fs.writeFileSync(markerFile, 'done');
  console.log(`[build_deps] External dependencies for ${targetArch} built successfully.`);
} catch (error) {
  console.error('[build_deps] Failed to build external dependencies:', error);
  process.exit(1);
}