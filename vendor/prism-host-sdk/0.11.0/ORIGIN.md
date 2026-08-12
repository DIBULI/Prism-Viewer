# Prism Host SDK 0.11.0 provenance

This directory contains the public headers and frozen platform runtimes used by
Prism Viewer 0.11.0. This release exposes Runtime API v4 and runtime SC130GS
gain control through exposure protocol v2. It must be used with Agent 0.11.0.

## Public headers

- Source tree: `prism-sdk/usb-sdk/include` from the 0.11.0 gain-control build.
- Release source commit: `1b2a09d338051c8c47075290036fd14650dcfcea`.
- Windows build source commit: `66c8cd808d6a398f5cafe258012458a03c1a07a0`.
- The vendored include tree is an exact copy of the headers used to build all
  three runtimes.

## Linux x86-64 runtime

- Build environment: Ubuntu 24.04, GCC 13.3, Release.
- Host SDK CTest: 6/6 passed.
- File: `runtime/linux-x64/libprism_usb_sdk.so`.
- SHA-256: `6510750ece0e2d1dc145f3c463635c03b068e5683a372c07dd1a99ce7a4a2819`.

## Windows x86-64 runtime

- Build environment: GitHub Actions `windows-2022`, MSVC x64, Release.
- Host SDK CTest: 6/6 passed before artifact publication.
- Workflow run: https://github.com/DIBULI/Prism-agent/actions/runs/31612830826
- Source commit: `66c8cd808d6a398f5cafe258012458a03c1a07a0`.
- File: `runtime/windows-x64/prism_usb_sdk.dll`.
- SHA-256: `c36707501abc95fa90604ade73e822be44ca2eebaa435fdec40a30ddc0c3ffbb`.

## macOS arm64 runtime

- Build environment: macOS arm64, Apple Clang, Release, deployment target
  macOS 13.0.
- Host SDK CTest: 6/6 passed.
- SDK file: `runtime/macos-arm64/libprism_usb_sdk.dylib`.
- SDK SHA-256: `293c4c519bec4c3026cf7b460f03aa63501a062eae84275caeea63c2e7dd8981`.
- libusb file: `runtime/macos-arm64/libusb-1.0.0.dylib`.
- libusb SHA-256: `329a4c0ea465b3ebf81c4d95165d25739380b84fbc77c3c7af9e11a83f7cea08`.
- libusb source: official libusb 1.0.30 release archive, SHA-256
  `fea36f34f9156400209595e300840767ab1a385ede1dc7ee893015aea9c6dbaf`.
- License file: `runtime/macos-arm64/libusb-COPYING.txt`, SHA-256
  `5df07007198989c622f5d41de8d703e7bef3d0e79d62e24332ee739a452af62a`.
- Both dylibs are arm64 Mach-O images with relocatable install names and no
  Homebrew or build-machine load path.

The one-purpose Windows build branch is removed after copying and verifying
the published DLL; it is not a product branch.
