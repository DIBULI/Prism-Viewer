# Prism Viewer

![Prism](branding/prism-logo.svg)

Prism Viewer is the standalone Qt desktop application for Prism USB devices.
It provides four-camera MJPEG preview, two onboard IMUs plus optional
Mid-360/Mid-360S IMU recording, LiDAR point-cloud display with adjustable point
rendering plus top/reset view presets, switchable live onboard-IMU display
units, per-camera runtime SC130GS exposure/gain control, device configuration,
system upgrade, timestamp-driven dataset playback at 0.25x through 8x, and ROS1/ROS2
bag export. IMU display units
are independent from the fixed SI units used by datasets and ROS bags. New v6
recordings use RK `CLOCK_REALTIME` with a Unix epoch as the
common device time domain for camera, onboard IMU, LiDAR point batches, and
LiDAR IMU. Absolute UTC accuracy is not required for stream alignment;
unsynchronized callbacks remain available for live preview but are never
written as measurement timestamps.

At startup, the Viewer scans for Prism USB devices. When exactly one device is
present it opens that device and automatically synchronizes RK
`CLOCK_REALTIME`, the Ethernet PHC, and the hardware RTC from the host clock
before capture is enabled. The clock status strip reports progress and the
final result. With multiple devices, select one and click **Open Device**; the
same one-time automatic synchronization runs after the first successful open.
If it fails, capture remains available and **Set Device Time** can retry it.

The Viewer does not compile or fetch Host SDK sources. The matching binary SDK
is versioned directly in this repository under
`vendor/prism-host-sdk/0.11.0`:

- public headers under `include/prism`;
- `prism_usb_sdk.dll` on Windows, loaded at runtime with `LoadLibraryW` and
  `GetProcAddress` (the import `.lib` is deliberately not used);
- `libprism_usb_sdk.so` on Linux, linked as a bundled shared library.
- `libprism_usb_sdk.dylib` plus its relocatable `libusb` runtime on Apple
  Silicon macOS, linked from the app's `Contents/Frameworks` directory.

The Viewer and bundled Host SDK versions must match exactly. Windows, Linux,
and macOS builds therefore need no Prism-agent checkout or separately
installed SDK.

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

On an Apple Silicon Mac, install Qt and run the native build script:

```sh
brew install cmake qtbase qtcharts qtdeclarative qtsvg
./scripts/build_macos.sh
open build-macos/stage/Prism-Viewer.app
```

Set `QT_ROOT` before invoking the script when Qt is installed under a custom
prefix. The script builds and tests arm64, installs the Viewer as a macOS app,
runs `macdeployqt`, and applies an ad-hoc local signature. The native Viewer
and SDK runtime use a macOS 13.0 deployment target. Distribution builds are not
notarized, so a downloaded archive may require **Open** from Finder's context
menu the first time it is launched.

The Viewer build does not need or look for an SDK import library. Its bundled
DLL exports `prism_usb_sdk_get_runtime_api` and uses the compatible MSVC 14.x
DLL runtime.

The build copies the Prism SDK runtime into the platform's application runtime
directory. Linux installation uses `$ORIGIN/../lib`; Windows deployment
bundles the SDK beside the executable; and macOS installs it in
`Prism-Viewer.app/Contents/Frameworks` with an
`@executable_path/../Frameworks` rpath. The dynamically bundled libusb uses
LGPL-2.1-or-later; its `libusb-COPYING.txt` license is included under the app's
`Contents/Resources/licenses` directory.

## Automated builds and releases

Every push and pull request builds and tests Windows x64, Linux x64, and macOS
arm64. A tag matching `v*` additionally publishes all packaged Viewer archives
as a GitHub Release. For example:

```sh
git tag v0.11.0
git push origin v0.11.0
```

## Documentation

- [Code structure](ARCHITECTURE.md)
- [Dataset directory structure and format](docs/dataset-format.md)
- [ROS1/ROS2 bag export](docs/rosbag-export.md)
