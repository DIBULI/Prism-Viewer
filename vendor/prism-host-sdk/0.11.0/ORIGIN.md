# Prism Host SDK 0.11.0 provenance

This directory contains the public headers and frozen platform runtimes used by
Prism Viewer 0.11.0. This release exposes Runtime API v5, USB plus 5 GHz WiFi
TCP device opening, runtime SC130GS gain control, dynamic exposure ceilings,
and every integer USB camera frame rate from 1 through 30 fps. WiFi TCP camera
preview is capped at 20 fps. It must be used with Agent 0.11.0.

## Public headers

- Source tree: `prism-sdk/usb-sdk/include` from the 0.11.0 WiFi TCP build.
- Release source commit: `1d240e9cae24876862aa39c56f5dc2ac98b0fddc`.
- Windows workflow commit: `b8fb901` (source parent `1d240e9`).
- The vendored include tree is an exact copy of the headers used to build all
  three runtimes.

## Linux x86-64 runtime

- Build environment: Ubuntu 24.04, GCC 13.3, Release.
- Host SDK CTest: 7/7 passed.
- File: `runtime/linux-x64/libprism_usb_sdk.so`.
- SHA-256: `5ad09b5d5f82dc8efb41f8fb08492f473fc6319caad2a22bf95ad62263e88743`.

## Windows x86-64 runtime

- Build environment: GitHub Actions `windows-2022`, MSVC x64, Release.
- Host SDK CTest: 7/7 passed before artifact publication.
- Workflow run: https://github.com/DIBULI/Prism-agent/actions/runs/31787245929
- Source commit: `1d240e9cae24876862aa39c56f5dc2ac98b0fddc`.
- File: `runtime/windows-x64/prism_usb_sdk.dll`.
- SHA-256: `75ffae595a541da86422ec44c1af2b5d1538f13739a1e81ebce9adb9251edc0f`.

## macOS arm64 runtime

- Build environment: macOS arm64, Apple Clang, Release, deployment target
  macOS 13.0.
- Host SDK CTest: 7/7 passed.
- SDK file: `runtime/macos-arm64/libprism_usb_sdk.dylib`.
- SDK SHA-256: `3bea57ba0ba4b563f951da6cb6e39f79bc95dbfec64cc5879db431b1ade8a26c`.
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
