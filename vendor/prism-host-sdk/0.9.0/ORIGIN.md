# Prism Host SDK 0.9.0 binary provenance

These public headers and x86-64 shared libraries are consumed by Prism Viewer
without checking out or compiling the Prism-agent repository.

- Source repository: `DIBULI/Prism-agent`
- Source commit: `d04e3b0c20019c5cf1799c5887226555781ba626`
- Windows build: `DIBULI/Prism-Viewer` Actions run `31360039501`, GitHub
  `windows-2022`, MSVC x64, Release DLL runtime
- Linux build: Ubuntu 22.04 Docker, GCC 11 x86-64, Release; SDK tests 6/6

SHA-256:

```text
0bd8cdce4b86de55939444c1de9d08df780216c163df4b31054067843b9f4ba5  runtime/windows-x64/prism_usb_sdk.dll
73978662b1124ae77c1e68edd26fd634a0f53be7dd57b20dd4c68931d6ba61b7  runtime/linux-x64/libprism_usb_sdk.so
```
