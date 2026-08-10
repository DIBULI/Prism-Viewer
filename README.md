# Prism Viewer

![Prism](branding/prism-logo.svg)

Prism Viewer is the standalone Qt desktop application for Prism USB devices.
It provides four-camera MJPEG preview, two onboard IMUs plus optional
Mid-360/Mid-360S IMU recording, LiDAR point-cloud display with adjustable point
rendering, device configuration, system upgrade, dataset browsing, and ROS1/ROS2
bag export.

The Viewer does not compile or fetch Host SDK sources. The matching binary SDK
is versioned directly in this repository under
`vendor/prism-host-sdk/0.9.0`:

- public headers under `include/prism`;
- `prism_usb_sdk.dll` on Windows, loaded at runtime with `LoadLibraryW` and
  `GetProcAddress` (the import `.lib` is deliberately not used);
- `libprism_usb_sdk.so` on Linux, linked as a bundled shared library.

The Viewer and bundled Host SDK versions must match exactly. Windows and Linux
builds therefore need no Prism-agent checkout or separately installed SDK.

## Build

Install Qt Widgets, Qt Charts, and Qt SQL/SQLite. On Ubuntu/Debian:

```sh
sudo apt install build-essential cmake qtbase5-dev libqt5charts5-dev \
  libqt5sql5-sqlite
./scripts/build_linux.sh
./build-linux/prism-viewer
```

For an Ubuntu 22.04 container:

```sh
./scripts/build_linux_docker.sh
```

On Windows, pass only the Qt prefix:

```bat
scripts\build_viewer_msvc.bat C:\Qt\6.11.1\msvc2022_64
```

The Viewer build does not need or look for an SDK import library. Its bundled
DLL exports `prism_usb_sdk_get_runtime_api` and uses the compatible MSVC 14.x
DLL runtime.

The build copies the Prism SDK runtime beside development/test executables.
Linux installation uses `$ORIGIN/../lib`; Windows deployment bundles the SDK
runtime together with the required Qt runtime.

## Automated builds and releases

Every push and pull request builds and tests Windows x64 and Linux x64. A tag
matching `v*` additionally publishes both packaged Viewer archives as a GitHub
Release. For example:

```sh
git tag v0.9.0
git push origin v0.9.0
```

## Documentation

- [Code structure](ARCHITECTURE.md)
- [Dataset format](docs/dataset-format.md)
- [ROS1/ROS2 bag export](docs/rosbag-export.md)
