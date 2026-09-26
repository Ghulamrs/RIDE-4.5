{
  "name": "thirdparty-mathx",
  // "arch" is the platform built for - the Target menu's choice.
  "arch": "arm64-darwin",
  // The third-party library: its headers, and its prebuilt binary for this platform.
  "include": ["third_party/mathx/include"],
  "libraries": ["third_party/mathx/libs/arm64-darwin/libmathx.a"],
  "groups": {
    "Sources": ["main.cpp"],
    "Library": ["third_party/mathx/include/mathx.h", "third_party/mathx/source/BUILD.txt"]
  },
  // "target" is the program the build makes (mathx-demo, or mathx-demo.exe on Windows).
  "build": { "target": "mathx-demo", "groups": ["Sources"] }
}
