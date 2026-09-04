# Prism Viewer

![Prism](branding/prism-logo.svg)

Prism Viewer is the standalone Qt desktop application for Prism USB devices.
It provides four-camera MJPEG preview, two onboard IMUs plus optional
Mid-360/Mid-360S IMU recording, LiDAR point-cloud display with adjustable point
rendering plus top/reset view presets, switchable live onboard-IMU display
units, per-camera runtime SC130GS exposure/gain control, device configuration,
system upgrade, synchronized camera/onboard-IMU/LiDAR/LiDAR-IMU dataset playback
at 0.25x through 8x, and ROS1/ROS2 bag export. IMU display units
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

The Viewer does not compile Host SDK sources. The matching binary SDK is
pinned as the `third_party/Prism-SDK` Git submodule:

- public headers under `include/prism`;
- `prism_usb_sdk.dll` on Windows, loaded at runtime with `LoadLibraryW` and
  `GetProcAddress` (the import `.lib` is deliberately not used);
- the unified Ubuntu 20.04-baseline `libprism_usb_sdk.so` on Linux x86-64;
- `libprism_usb_sdk.a` on Linux arm64, linked into the Viewer executable;
- `libprism_usb_sdk.dylib` plus its relocatable `libusb` runtime on Apple
  Silicon macOS, linked from the app's `Contents/Frameworks` directory.

The Viewer and SDK submodule versions must match exactly. Windows, Linux, and
macOS builds therefore need no Prism-agent checkout or separately installed
SDK. Clone with submodules enabled:

```sh
git clone --recurse-submodules git@github.com:DIBULI/Prism-Viewer.git
```

For an existing checkout, initialize the pinned SDK with:

```sh
git submodule update --init --recursive
```

## Build

Install Qt Widgets, Qt Charts, and Qt SQL/SQLite. On Ubuntu/Debian:

```sh
sudo apt install build-essential cmake qtbase5-dev libqt5charts5-dev \
  libqt5sql5-sqlite
./scripts/build_linux.sh
./build-linux/prism-viewer
```

### Linux USB permissions

Install the udev rule for the Prism-A4L USB device (`2207:1201`):

```sh
sudo tee /etc/udev/rules.d/99-prism-usb.rules >/dev/null <<'EOF'
# DIBULI Prism-A4L
SUBSYSTEM=="usb", ATTR{idVendor}=="2207", ATTR{idProduct}=="1201", MODE="0660", GROUP="plugdev", TAG+="uaccess"
EOF

sudo groupadd -f plugdev
sudo usermod -aG plugdev "$USER"
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=usb --action=add
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

Every push and pull request checks out the pinned SDK submodule, then builds
and tests Windows x64, Linux x64/arm64, and macOS arm64. A tag matching `v*`
additionally publishes all packaged Viewer archives as a GitHub Release. For
example:

```sh
git tag v1.0.1
git push origin v1.0.1
```

All release archives include the Viewer, the matching Prism Host SDK runtime,
Qt libraries and plugins, compiler runtime libraries, and recursively linked
third-party libraries. The Linux x86-64 SDK statically embeds OpenSSL and the
ARM64 package carries the matching OpenSSL runtime; both packages include
libusb and the Qt XCB, JPEG, and SQLite plugins. Users therefore do not need to
install those packages separately. Linux still relies on the target system's
kernel, glibc, and hardware/display drivers. Windows and Linux archives contain
a SHA-256 file manifest, and CI checks every packaged Linux binary in a minimal
Ubuntu image before publishing a release.

Linux x64 and arm64 releases are built inside Ubuntu 20.04 and require glibc
2.31 or newer. CI extracts each tar archive and starts the packaged Viewer in
Ubuntu 20.04, 22.04, 24.04, and 26.04 containers before publishing it:

```sh
tar -xzf Prism-Viewer-1.0.1-linux-x64.tar.gz
./Prism-Viewer-1.0.1-linux-x64/bin/prism-viewer

# On an arm64 host:
tar -xzf Prism-Viewer-1.0.1-linux-arm64.tar.gz
./Prism-Viewer-1.0.1-linux-arm64/bin/prism-viewer
```

The Windows x64 release supports Windows 10 version 1809 or newer and Windows
11. Windows ARM64 is not currently supported. The macOS arm64 release targets
macOS 13.0 and supports macOS 13 Ventura, macOS 14 Sonoma, and macOS 15
Sequoia on Apple Silicon; Intel Macs and macOS 12 or earlier are not supported.

The Linux x86-64 Viewer uses the single Prism SDK `.so` built on Ubuntu 20.04
for all supported Ubuntu releases. The Linux ARM64 Viewer links the SDK
implementation statically. Qt still loads platform, image, and SQL plugins
dynamically; the tar archives include all required private runtime dependencies.

## Documentation

- [Prism Viewer 1.0.1 update notes](docs/update/v1.0.1.md)
- [Prism Viewer 1.0.1 更新说明](docs/update/v1.0.1.zh-CN.md)
- [Prism Viewer 1.0.0 update notes](docs/update/v1.0.0.md)
- [Prism Viewer 1.0.0 更新说明](docs/update/v1.0.0.zh-CN.md)
- [Prism Viewer 1.0.0 中文用户操作手册](docs/Prism-Viewer-1.0.0-用户操作手册.pdf)
- [操作手册生成与截图打码脚本](docs/manual/README.md)
- [Code structure](ARCHITECTURE.md)
- [Dataset directory structure and format](docs/dataset-format.md)
- [ROS1/ROS2 bag export](docs/rosbag-export.md)

## Recorded dataset playback

When browsing a local dataset, the frame progress slider occupies an
independent full-width row, so changing per-frame exposure text does not resize
it. Use **Go to frame** to enter a 1-based frame number and jump directly to
that camera frame while keeping camera, onboard-IMU, LiDAR, and LiDAR-IMU
playback aligned to its timestamp.
