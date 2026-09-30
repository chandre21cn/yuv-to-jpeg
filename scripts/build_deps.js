const { execSync } = require('child_process');
const fs = require('fs');
const path = require('path');

// 将依赖库安装在项目根目录下的 external_deps 文件夹中
const projectRootDir = path.resolve(__dirname, '..');
const externalDir = path.join(projectRootDir, 'external_deps');
const markerFile = path.join(externalDir, 'external_built.marker');

// 如果已经编译过，则跳过
if (fs.existsSync(markerFile)) {
  console.log('[build_deps] External dependencies already built.');
  process.exit(0);
}

fs.mkdirSync(externalDir, { recursive: true });

function runCommand(cmd, cwd) {
  console.log(`[build_deps] Running: ${cmd}`);
  execSync(cmd, { stdio: 'inherit', cwd: cwd || process.cwd() });
}

function buildLibTurboJpeg() {
  const prefix = path.join(externalDir, 'libjpeg-turbo');
  const srcDir = path.join(prefix, 'src');
  const buildDir = path.join(prefix, 'build');
  const installDir = path.join(prefix, 'install');

  if (!fs.existsSync(srcDir)) {
    runCommand(`git clone --depth 1 --branch 3.2.0 https://github.com/libjpeg-turbo/libjpeg-turbo.git "${srcDir}"`);
  }

  fs.mkdirSync(buildDir, { recursive: true });

  const extraArgs = process.platform === 'win32' ? '-DCMAKE_CXX_FLAGS="/utf-8" -DCMAKE_C_FLAGS="/utf-8"' : '';
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
  const buildDir = path.join(prefix, 'build');
  const installDir = path.join(prefix, 'install');

  if (!fs.existsSync(srcDir)) {
    runCommand(`git clone --depth 1 https://chromium.googlesource.com/libyuv/libyuv "${srcDir}"`);
  }

  fs.mkdirSync(buildDir, { recursive: true });

  const extraArgs = process.platform === 'win32' ? '-DCMAKE_CXX_FLAGS="/utf-8" -DCMAKE_C_FLAGS="/utf-8"' : '';
  const cmakeCmd = `cmake -S "${srcDir}" -B "${buildDir}" ` +
    `-DCMAKE_INSTALL_PREFIX="${installDir}" ` +
    `-DCMAKE_BUILD_TYPE=Release -DUNIT_TEST=OFF ${extraArgs}`;

  runCommand(cmakeCmd);
  runCommand(`cmake --build "${buildDir}" --config Release --target install`);
}

try {
  buildLibTurboJpeg();
  buildLibYuv();
  fs.writeFileSync(markerFile, 'done');
  console.log('[build_deps] External dependencies built successfully.');
} catch (error) {
  console.error('[build_deps] Failed to build external dependencies:', error);
  process.exit(1);
}