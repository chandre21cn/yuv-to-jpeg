{
  "variables": {
    # TurboJPEG（libjpeg-turbo）安装根目录，要求包含 include/turbojpeg.h 和 lib/
    # 默认 deps/libjpeg-turbo，可覆盖：
    #   node-gyp rebuild --turbojpeg_root=/usr/local/opt/jpeg-turbo
    #   node-gyp rebuild --turbojpeg_root=C:/libjpeg-turbo64
    "turbojpeg_root%": "<(module_root_dir)/deps/libjpeg-turbo",
    "turbojpeg_static%": 1
  },
  "targets": [
    {
      "target_name": "yuv2jpeg_worker",
      "sources": [
        "src/addon.cc",
        "src/watermark.cc",
        "src/encode_context.cc",
        "src/encode_worker.cc",
        "src/pixel_buffer_encoder.cc",
        "src/jpeg_encoder.cc"
      ],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")",
        "<(turbojpeg_root)/include"
      ],
      "defines": [
        "NAPI_VERSION=8",
        "NAPI_CPP_EXCEPTIONS",
        "NOMINMAX"
      ],
      "cflags!":    [ "-fno-exceptions" ],
      "cflags_cc!": [ "-fno-exceptions", "-fno-rtti" ],
      "cflags_cc":  [ "-std=c++17", "-fexceptions", "-O3", "-fvisibility=hidden",
                      "-ffunction-sections", "-fdata-sections" ],
      "cflags":     [ "-ffunction-sections", "-fdata-sections" ],
      "conditions": [
        ["OS=='win'", {
          "defines": [ "WIN32_LEAN_AND_MEAN", "_CRT_SECURE_NO_WARNINGS" ],
          "msvs_settings": {
            "VCCLCompilerTool": {
              "ExceptionHandling": 1,
              "AdditionalOptions": [ "/std:c++17", "/O2", "/Oi", "/Ot", "/Gy" ]
            },
            "VCLinkerTool": {
              "AdditionalOptions": [ "/OPT:REF", "/OPT:ICF" ]
            }
          },
          "conditions": [
            ["turbojpeg_static==1", {
              "libraries": [ "<(turbojpeg_root)/lib/turbojpeg-static.lib" ]
            }, {
              "libraries": [ "<(turbojpeg_root)/lib/turbojpeg.lib" ]
            }]
          ]
        }],
        ["OS=='mac'", {
          "xcode_settings": {
            "GCC_ENABLE_CPP_EXCEPTIONS": "YES",
            "GCC_ENABLE_CPP_RTTI": "YES",
            "CLANG_CXX_LANGUAGE_STANDARD": "c++17",
            "CLANG_CXX_LIBRARY": "libc++",
            "GCC_OPTIMIZATION_LEVEL": "3",
            "GCC_SYMBOLS_PRIVATE_EXTERN": "YES",
            "GCC_DEAD_CODE_STRIPPING": "YES",
            "OTHER_LDFLAGS": [ "-Wl,-dead_strip" ],
            "MACOSX_DEPLOYMENT_TARGET": "10.15"
          },
          "conditions": [
            ["turbojpeg_static==1", {
              "libraries": [ "<(turbojpeg_root)/lib/libturbojpeg.a" ]
            }, {
              "libraries": [
                "-L<(turbojpeg_root)/lib",
                "-lturbojpeg",
                "-Wl,-rpath,@loader_path"
              ]
            }]
          ]
        }],
        ["OS=='linux'", {
          "ldflags": [ "-Wl,--gc-sections" ],
          "conditions": [
            ["turbojpeg_static==1", {
              "libraries": [ "<(turbojpeg_root)/lib/libturbojpeg.a" ]
            }, {
              "libraries": [
                "-L<(turbojpeg_root)/lib",
                "-lturbojpeg",
                "-Wl,-rpath,'$$ORIGIN'"
              ]
            }]
          ]
        }]
      ]
    }
  ]
}