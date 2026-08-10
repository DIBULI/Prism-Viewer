# Prism Host SDK 0.10.0 provenance

The public headers and platform runtimes in this directory come from the
Prism-agent commits recorded below. This release exposes Runtime API v3 and the
v2 LiDAR point-batch timestamp fields; it must not be mixed with a 0.9 Agent or
runtime.

## Public headers

- Source: `prism-sdk/usb-sdk/include` at Agent commit `44b7d9564d4aa50c78e1295b67551613047e9615`
- The vendored include tree is an exact byte-for-byte copy of that source tree.

## Linux x86-64 runtime

- Build environment: Ubuntu 22.04 Docker image, GCC 11.4, Release
- Host SDK CTest: 6/6 passed
- File: `runtime/linux-x64/libprism_usb_sdk.so`
- SHA-256: `e6512bf415007ed67d0c9703602f898e589997ba6d653eb02cee78ece625409e`

## Windows x86-64 runtime

- Build environment: GitHub Actions `windows-2022`, MSVC x64, Release
- Host SDK CTest: 6/6 passed before the workflow published the DLL
- Temporary workflow commit: `87f619951377d3052fe4d568bac33a16426940e6`
- Build-result commit: `bc3d7f2d4a15d0537c2390fe6282cb9bb68e7314`
- File: `runtime/windows-x64/prism_usb_sdk.dll`
- SHA-256: `1d92ac9a1f60f85e71bb16d9e55968c47d8bd113dbc679d9b07f5e18ef1aec92`

## macOS arm64 runtime

- Agent source: `44b7d9564d4aa50c78e1295b67551613047e9615`
  (`feat(sdk): support native macOS hosts`)
- Build environment: macOS arm64, Apple Clang 15.0.0, macOS 14.4 SDK,
  Release, deployment target 13.0
- Host SDK shared CTest: 6/6 passed
- Host SDK static CTest and installed-package consumer: 6/6 passed and loaded
- SDK file: `runtime/macos-arm64/libprism_usb_sdk.dylib`
- SDK SHA-256: `dbe0057a339c64ccce9221041e948d421fa32ead2cdd47904187a66ac45e1f8f`
- libusb file: `runtime/macos-arm64/libusb-1.0.0.dylib`
- libusb SHA-256: `329a4c0ea465b3ebf81c4d95165d25739380b84fbc77c3c7af9e11a83f7cea08`
- libusb source: official libusb 1.0.30 release archive, SHA-256
  `fea36f34f9156400209595e300840767ab1a385ede1dc7ee893015aea9c6dbaf`
- License file: `runtime/macos-arm64/libusb-COPYING.txt`, SHA-256
  `5df07007198989c622f5d41de8d703e7bef3d0e79d62e24332ee739a452af62a`
- Both dylibs are arm64 Mach-O images with `minos=13.0`; their install names,
  dependencies, and runtime search path are relocatable and contain no build
  machine or package-manager path.

The one-purpose Windows build branch contained only the frozen Agent source,
the workflow, and its temporary `ci-output` result. The Windows runtime copied
here is the same file recorded by the build-result commit.
