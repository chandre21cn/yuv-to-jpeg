const path = require('path');
const fs = require('fs');

const binaryPath = path.join(__dirname, 'build', 'Release', 'yuv2jpeg_worker.node');

if (!fs.existsSync(binaryPath)) {
    throw new Error(
        `yuv-to-jpeg: native binary not found at ${binaryPath}\n` +
        'Run "npm install" again, or build from source with "npm run build".'
    );
}

module.exports = require(binaryPath);
