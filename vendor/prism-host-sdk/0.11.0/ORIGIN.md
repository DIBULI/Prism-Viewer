# Prism Host SDK 0.11.0 provenance

This directory contains the public headers and frozen platform runtimes used by
Prism Viewer 0.11.0. This release exposes Runtime API v4 and runtime SC130GS
gain control through exposure protocol v2. The exposure range supports the
10/20/30 FPS limits of 95000/45000/28333 us and defaults to 1x sensor gain.
It must be used with Agent 0.11.0.

## Public headers

- Source tree: `prism-sdk/usb-sdk/include` from the 0.11.0 exposure-window build.
- Runtime build source commit: `499c347` (`feat(camera): derive exposure limits from frame rate`).
- The vendored include tree is an exact copy of the headers used to build all
  three runtimes.

## Linux x86-64 runtime

- Build environment: Ubuntu 24.04, GCC 13.3, Release.
- Host SDK CTest: 6/6 passed.
- File: `runtime/linux-x64/libprism_usb_sdk.so`.
- SHA-256: `1fa6375dc8a606d9d7725d5ac39132cc73151250402b4c77fb7dad547cdfd9ce`.

## Windows x86-64 runtime

- Build environment: GitHub Actions `windows-2022`, MSVC x64, Release.
- Host SDK CTest: 6/6 passed before artifact publication.
- Workflow run: https://github.com/DIBULI/Prism-agent/actions/runs/31627472687
- Source commit: `499c347` (`feat(camera): derive exposure limits from frame rate`).
- File: `runtime/windows-x64/prism_usb_sdk.dll`.
- SHA-256: `6fb130754480e31329ae43a8d9d8ede65e9d823e20da946d0e8c440cd5e2fe99`.

## macOS arm64 runtime

- Build environment: macOS arm64, Apple Clang, Release, deployment target
  macOS 13.0.
- Host SDK CTest: 6/6 passed.
- SDK file: `runtime/macos-arm64/libprism_usb_sdk.dylib`.
- SDK SHA-256: `01267230552b62fd6b38345c275b26dba547edd2336fd0ba3cd9f6e5b6ab1251`.
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
