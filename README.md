# Prism Viewer

![Prism](branding/prism-logo.svg)

Prism Viewer is the standalone Qt desktop application for Prism USB devices.
It provides four-camera MJPEG preview, dual-IMU monitoring and recording,
Mid-360/Mid-360S point-cloud display, device configuration, system upgrade,
dataset browsing, and ROS1/ROS2 bag export.

The Viewer does not compile or vendor Host SDK sources. It consumes only the
installed Prism Host SDK package:

- public headers under `include/prism`;
- `prism_usb_sdk.dll` on Windows, loaded at runtime with `LoadLibraryW` and
  `GetProcAddress` (the import `.lib` is deliberately not used);
- `libprism_usb_sdk.so` on Linux;
- `libprism_usb_sdk.dylib` on macOS;
- `lib/cmake/PrismUsbSdk/PrismUsbSdkConfig.cmake` on all platforms.

The Viewer and Host SDK versions must match exactly. This source currently
requires Prism Host SDK `0.9.0`.

## Build

Install Qt Widgets, Qt Charts, and Qt SQL/SQLite, then pass the installed SDK
prefix. On Ubuntu/Debian:

```sh
sudo apt install build-essential cmake qtbase5-dev libqt5charts5-dev \
  libqt5sql5-sqlite
./scripts/build_linux.sh /opt/prism-host-sdk-0.9.0
./build-linux/prism-viewer
```

For an Ubuntu 22.04 container, the SDK prefix must contain Linux binaries:

```sh
./scripts/build_linux_docker.sh /opt/prism-host-sdk-0.9.0
```

On macOS:

```sh
brew install cmake qtbase qtcharts
./scripts/build_macos.sh /opt/prism-host-sdk-0.9.0
open "build-macos/Prism Viewer.app"
```

On Windows, pass the Qt prefix first and SDK prefix second:

```bat
scripts\build_viewer_msvc.bat C:\Qt\6.11.1\msvc2022_64 C:\Prism\HostSdk-0.9.0
```

The Windows SDK prefix only needs the matching public headers and
`bin\prism_usb_sdk.dll`; the Viewer build does not need or look for an import
library. The DLL must export `prism_usb_sdk_get_runtime_api` and must be built
with a compatible MSVC 14.x runtime from the exact same SDK version.

The build copies the Prism SDK runtime beside development/test executables.
Linux installation uses `$ORIGIN/../lib`; macOS and Windows deployment scripts
bundle the SDK runtime together with the required Qt runtime.

## Documentation

- [Code structure](ARCHITECTURE.md)
- [Dataset format](docs/dataset-format.md)
- [ROS1/ROS2 bag export](docs/rosbag-export.md)
