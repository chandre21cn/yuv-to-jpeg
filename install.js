// postinstall 脚本：从 GitHub Releases 下载当前平台对应的预编译 .node 文件
// 用法：在 package.json 的 scripts.postinstall 中调用 "node install.js"

const fs = require('fs');
const path = require('path');
const https = require('https');

const pkg = require('./package.json');
const { platform, arch } = process;

// 从 package.json 的 repository 字段解析 GitHub owner/repo
// 支持格式："https://github.com/owner/repo.git" 或 "owner/repo"
function parseGithubRepo(repoUrl) {
    if (!repoUrl) return null;
    const match = repoUrl.match(/github\.com[\/:]([^\/]+)\/([^\/.]+?)(?:\.git)?$/);
    if (match) return { owner: match[1], repo: match[2] };
    // 短格式 owner/repo
    const short = repoUrl.match(/^([^\/]+)\/([^\/]+)$/);
    if (short) return { owner: short[1], repo: short[2] };
    return null;
}

const repoInfo = parseGithubRepo(pkg.repository && pkg.repository.url);
if (!repoInfo) {
    console.warn('[yuv-to-jpeg] Cannot parse GitHub repository from package.json.');
    console.warn('[yuv-to-jpeg] Please set "repository.url" to your GitHub repo, or build from source: npm run build');
    process.exit(0);
}

const GITHUB_OWNER = repoInfo.owner;
const GITHUB_REPO = repoInfo.repo;
const VERSION = pkg.version;

// 支持的平台映射（Node process.platform → Release 资源后缀）
const PLATFORM_MAP = {
    darwin: 'darwin',
    win32: 'win32',
    linux: 'linux',
};

const ARCH_MAP = {
    arm64: 'arm64',
    x64: 'x64',
};

const osName = PLATFORM_MAP[platform];
const archName = ARCH_MAP[arch];

if (!osName || !archName) {
    console.warn(
        `[yuv-to-jpeg] Unsupported platform: ${platform}-${arch}.\n` +
        'Supported: darwin-arm64, win32-x64, win32-arm64\n' +
        'Please build from source: npm run build'
    );
    process.exit(0);
}

const assetName = `yuv_to_jpeg-${osName}-${archName}.node`;
const downloadUrl = `https://github.com/${GITHUB_OWNER}/${GITHUB_REPO}/releases/download/v${VERSION}/${assetName}`;

const targetDir = path.join(__dirname, 'build', 'Release');
const targetPath = path.join(targetDir, 'yuv2jpeg_worker.node');

function download(url, dest) {
    return new Promise((resolve, reject) => {
        const file = fs.createWriteStream(dest);
        https.get(url, (res) => {
            // 处理重定向
            if (res.statusCode >= 300 && res.statusCode < 400 && res.headers.location) {
                file.close();
                fs.unlinkSync(dest);
                download(res.headers.location, dest).then(resolve, reject);
                return;
            }
            if (res.statusCode !== 200) {
                file.close();
                fs.unlink(dest, () => {});
                reject(new Error(`HTTP ${res.statusCode} for ${url}`));
                return;
            }
            res.pipe(file);
            file.on('finish', () => file.close(resolve));
        }).on('error', (err) => {
            file.close();
            fs.unlink(dest, () => {});
            reject(err);
        });
    });
}

async function main() {
    // 已存在则跳过（开发环境本地编译过）
    if (fs.existsSync(targetPath)) {
        console.log('[yuv-to-jpeg] Native binary already exists, skipping download.');
        return;
    }

    fs.mkdirSync(targetDir, { recursive: true });

    console.log(`[yuv-to-jpeg] Downloading ${assetName} ...`);
    console.log(`[yuv-to-jpeg]   ${downloadUrl}`);

    try {
        await download(downloadUrl, targetPath);
        const size = (fs.statSync(targetPath).size / 1024).toFixed(0);
        console.log(`[yuv-to-jpeg] Downloaded ${assetName} (${size} KB)`);
    } catch (err) {
        console.error(`[yuv-to-jpeg] Failed to download binary: ${err.message}`);
        console.error(`[yuv-to-jpeg] URL: ${downloadUrl}`);
        console.error('[yuv-to-jpeg] Please check your network or the release exists for this version.');
        fs.rmSync(targetPath, { force: true });
        process.exit(1);
    }
}

main();
