# Prism Host SDK 0.11.0 provenance

This directory contains the public headers and frozen platform runtimes used by
Prism Viewer 0.11.0. This release exposes Runtime API v4, runtime SC130GS gain
control, dynamic exposure ceilings, and every integer camera frame rate from 1
through 30 fps. It must be used with Agent 0.11.0.

## Public headers

- Source tree: `prism-sdk/usb-sdk/include` from the 0.11.0 FPS build.
- Release source commit: `9cff8a701069684d51112bf1b3dc64b0c4ff8c0d`.
- Windows build source commit: `9cff8a701069684d51112bf1b3dc64b0c4ff8c0d`.
- The vendored include tree is an exact copy of the headers used to build all
  three runtimes.

## Linux x86-64 runtime

- Build environment: Ubuntu 24.04, GCC 13.3, Release.
- Host SDK CTest: 6/6 passed.
- File: `runtime/linux-x64/libprism_usb_sdk.so`.
- SHA-256: `b03d1dc616e5a489f519fd7816805ef985542329019c05b517c01dc0cbdbe9d0`.

## Windows x86-64 runtime

- Build environment: GitHub Actions `windows-2022`, MSVC x64, Release.
- Host SDK CTest: 6/6 passed before artifact publication.
- Workflow run: https://github.com/DIBULI/Prism-agent/actions/runs/31666461070
- Source commit: `9cff8a701069684d51112bf1b3dc64b0c4ff8c0d`.
- File: `runtime/windows-x64/prism_usb_sdk.dll`.
- SHA-256: `e252a51b54fb03c4615ca54d14d7b217c810f99933518e6e83102e82de2df7f6`.

## macOS arm64 runtime

- Build environment: macOS arm64, Apple Clang, Release, deployment target
  macOS 13.0.
- Host SDK CTest: 6/6 passed.
- SDK file: `runtime/macos-arm64/libprism_usb_sdk.dylib`.
- SDK SHA-256: `2edd7504146fb5d74c631171c7327d348af54522d28552747a6c88d3b46f41c7`.
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
