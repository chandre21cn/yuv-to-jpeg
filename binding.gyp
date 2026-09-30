{
  "targets": [
    {
      "target_name": "yuv_to_jpeg_builder",
      "type": "none",
      "actions": [
        {
          "action_name": "build_external_deps",
          "inputs": [],
          "outputs": [
            "<(module_root_dir)/external_deps/external_built.marker"
          ],
          "action": [
            "node",
            "scripts/build_deps.js"
          ]
        }
      ]
    },
    {
      "target_name": "yuv_to_jpeg",
      "dependencies": [
        "yuv_to_jpeg_builder"
      ],
      "sources": [
        "src/yuv_to_jpeg.cpp"
      ],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")",
        "<(module_root_dir)/external_deps/libjpeg-turbo/install/include",
        "<(module_root_dir)/external_deps/libyuv/install/include"
      ],
      "cflags_cc": [
        "-std=c++17",
        "-fexceptions"
      ],
      "conditions": [
        ["OS=='win'", {
          "msvs_settings": {
            "VCCLCompilerTool": {
              "CLLanguageStandard": "stdcpp17",
              "ExceptionHandling": 1, # 开启 /EHsc 异常支持
              "AdditionalOptions": [
                "/utf-8",
                "/O2",
                "/Oi",
                "/Ot",
                "/Gy",
                "/Gw"
              ]
            },
            "VCLinkerTool": {
              "EnableCOMDATFolding": "true",
              "OptimizeReferences": "true"
            }
          },
          "conditions": [
            ["target_arch=='x64'", {
              "msvs_settings": {
                "VCCLCompilerTool": {
                  "EnableEnhancedInstructionSet": "5" # AVX2
                }
              }
            }]
          ],
          "libraries": [
            "<(module_root_dir)/external_deps/libjpeg-turbo/install/lib/turbojpeg-static.lib",
            "<(module_root_dir)/external_deps/libyuv/install/lib/yuv.lib",
            "legacy_stdio_definitions.lib"
          ]
        }],
        ["OS!='win'", {
          "cflags": [
            "-O3",
            "-ffunction-sections",
            "-fdata-sections",
            "-fexceptions"
          ],
          "conditions": [
            ["target_arch=='x64'", {
              "cflags": [ "-mavx2" ]
            }],
            ["target_arch=='arm64'", {
              "cflags": [ "-fvectorize" ]
            }],
            ["OS=='mac'", {
              "xcode_settings": {
                "CLANG_CXX_LANGUAGE_STANDARD": "c++17",
                "GCC_ENABLE_CPP_EXCEPTIONS": "YES", # Xcode 显式开启 C++ 异常
                "MACOSX_DEPLOYMENT_TARGET": "13.5",
                "OTHER_CFLAGS": [
                  "-O3",
                  "-ffunction-sections",
                  "-fdata-sections",
                  "-fexceptions"
                ],
                "OTHER_LDFLAGS": [
                  "-Wl,-dead_strip"
                ]
              },
              "conditions": [
                ["target_arch=='x64'", {
                  "xcode_settings": {
                    "OTHER_CFLAGS": [ "-mavx2" ]
                  }
                }],
                ["target_arch=='arm64'", {
                  "xcode_settings": {
                    "OTHER_CFLAGS": [ "-fvectorize" ]
                  }
                }]
              ],
              "libraries": [
                "<(module_root_dir)/external_deps/libjpeg-turbo/install/lib/libturbojpeg.a",
                "<(module_root_dir)/external_deps/libyuv/install/lib/libyuv.a"
              ]
            }],
            ["OS=='linux'", {
              "ldflags": [
                "-Wl,--gc-sections"
              ],
              "libraries": [
                "-L<(module_root_dir)/external_deps/libjpeg-turbo/install/lib",
                "-L<(module_root_dir)/external_deps/libjpeg-turbo/install/lib64",
                "-lturbojpeg",
                "-L<(module_root_dir)/external_deps/libyuv/install/lib",
                "-L<(module_root_dir)/external_deps/libyuv/install/lib64",
                "-lyuv"
              ]
            }]
          ]
        }]
      ]
    }
  ]
}