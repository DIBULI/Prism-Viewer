# Prism Viewer 1.3.0

Matches the pinned public **Prism SDK 1.3.0** (core Runtime API 19, Network API 4,
Profile API 1, CameraControl API 1), Agent 1.3.0 and Sensor Board 0.5.0. Headers and binaries must
come from the same current package; an older download with the same version
number is not interchangeable. See [update notes](docs/update/v1.3.0.md).

DeviceInfo selects the product automatically; no Linux-only environment switch
is required. 41M3 keeps its existing MJPEG capture and exposure controls. 415M
requests four-camera external-trigger 2 FPS RAW12/packed12 from matching firmware.
1315 uses its mixed-profile capture entry. Unknown models fail before START.
A missing camera does not stop other camera previews or IMU callbacks.

415M USB data is preserved as full RAW12 for recording and zoom. Ordinary live
tiles are downsampled in Viewer; bounded per-camera preview replacement does not
drop recorded data. RAW12 recording/playback validates lengths and CRC, keeps
source identity, and reports missing data. RAW12 ROS export is not implemented.

The current 415M panel uses native integer-row exposure and manual gain per
camera. AE/AGC switches, brightness target/deadband and exposure/gain bounds
are global settings shared by all cameras.
Accepted configuration is distinct from
per-frame observations. Common-trigger timestamps are displayed only when the
validated frame reports that timing mode. CFA-aware thumbnail generation fixes
checkerboard sampling without altering original RAW12 pixels or recordings.

**Network connection** accepts Wi-Fi or Ethernet and the device's **Web admin
login password**. The password is session-only; old TLS certificate pairing and
separate access-token settings are removed. PRISM-NET/4 is plaintext TCP: use
only a trusted local network. This does not remove Web login authentication.
Recording remains subject to the confirmed USB 3.x recording gate below.

Platform builds and synthetic regression tests are not a new physical-device
acceptance test. This publication does not flash, restart or operate a device.

![Prism](branding/prism-logo.svg)

Prism Viewer is the standalone Qt desktop application for Prism USB devices.
It provides four-camera MJPEG preview, two onboard IMUs plus optional
Mid-360/Mid-360S IMU recording, LiDAR point-cloud display with adjustable point
rendering plus top/reset view presets, switchable live onboard-IMU display
units, per-camera runtime SC130GS exposure/gain control, device configuration,
system upgrade, CORS/NTRIP correction forwarding to the Agent, synchronized
camera/onboard-IMU/LiDAR/LiDAR-IMU dataset playback
at 0.25x through 8x, and ROS1/ROS2 bag export. IMU display units
are independent from the fixed SI units used by datasets and ROS bags. New v6
recordings declare either Unix time (`rk-clock-realtime`) or Sensor Board
boot time (`sensor-board-clock`) as the common device time domain for camera,
onboard IMU, LiDAR point batches, and LiDAR IMU. The Sensor Board is the master
clock; absolute UTC accuracy is not required for stream alignment;
unsynchronized callbacks remain available for live preview but are never
written as measurement timestamps.

At startup, the Viewer scans for Prism USB devices. When exactly one device is
present it opens that device automatically, but opening a device never changes
its clock. With multiple devices, select one and click **Open Device**. The
clock status strip reports the time source observed from the device. Use
**Set Device Time** explicitly while streams are idle if Host-based time
synchronization is required; the action remains disabled while external GPS is
the authoritative time source.

## USB recording

Viewer recording (full dataset and IMU-only) requires a confirmed **USB 3.0 or
faster** link, not merely a USB 3-capable port. USB 2.0, disconnected, unknown or
stale link status disables recording. The link is rechecked before creating any
files; loss of confirmation during recording finalizes the received data and
shows a warning without requesting capture to stop. Reconnect/refresh Device
Info and start a new recording after restoring USB 3.x. Offline dataset playback
and RK-local Web recording are unaffected.

## CORS / RTK

The **RTK → Sky / trajectories** page provides a rotatable satellite sky, a sortable,
filterable satellite table, and separate GGA and ADRNAV trajectories in local
ENU metres. It uses SDK read-only telemetry and never starts CORS. See
[GNSS / RTK visualization](docs/gnss-visualization.md) for data requirements,
freshness, gaps, and the distinction between GGA and the receiver's RTK solution.

GPS/GNSS details show receiver fix, satellites/DOP, position, UTC, and basic
PPS timing. Receiver-native GGA and ADRNAV results remain separate in the
graphics tab. The retired Agent software RTK raw/smoothed queries, result
widgets and `gps_rtk.csv` recording/playback are removed; legacy result
datasets are unsupported. Existing files are never migrated or deleted.
This source requires matching SDK headers/libraries with Runtime ABI 19.
41M3 retains four-camera unified automatic exposure with active-camera feedback
and highlight protection. 415M uses the separate native control panel
described above (requires the current matching firmware).
The current firmware publishes only internally aligned IMU samples; startup or
re-alignment can briefly pause IMU output while other streams continue. Internal
alignment is not UTC lock. See the [user guide](docs/manual/README.md).

MID360/MID360S recordings and ROS exports preserve per-point `line` and
`line_valid`; see [point metadata](docs/livox-line.md).

GNSS status, CORS connection settings, and plots use separate full-width pages
inside **RTK**, rather than three side-by-side columns.

Open a USB device, then use **RTK → CORS connection** to configure and start an
NTRIP correction session. Select **中国移动 CORS** or **千寻 CORS** in
**Service provider**, then enter that provider's username and password.

| Provider | Caster | WGS84 | CGCS2000 | Mountpoints |
| --- | --- | --- | --- | --- |
| China Mobile (`china_mobile`) | `120.253.226.97`, backup `120.253.239.161` | 8002 | 8001 | `RTCM33_GRCEJ`, `RTCM33_GRCEpro`, `RTCM33_GRCE`, `RTCM33_GRC`, `RTCM30_GR` |
| Qianxun / 千寻 (`qianxun`) | `203.107.45.154` | 8002 | 8003 | `AUTO` (五星十六频), `RTCM32_GGB` (三星八频), `RTCM30_GG` (双星) |

For Qianxun, start with `AUTO` and choose the coordinate system supplied with
your account. Switching providers preserves the coordinate-system choice but
updates its port: Qianxun CGCS2000 uses **8003**, not China Mobile's 8001.
Only China Mobile has a preset backup endpoint. The native RTK module uses the
selected primary IPv4/non-TLS endpoint; it does not inherit Viewer socket
failover, hostname or TLS support. Account validity is managed
by the provider; check expiry/renewal if authentication is rejected.

The retained legacy socket transport (not used by the native connection UI)
also supports the following China Mobile options:

- automatic primary-to-backup caster failover;
- an editable caster address accepting an IPv4/IPv6 address, hostname,
  host:port, or HTTP(S)/NTRIP(S) URL; an optional URL path supplies the
  mountpoint;
- WGS84 port 8002 and CGCS2000 port 8001;
- the RTCM33_GRCEJ, RTCM33_GRCEpro, RTCM33_GRCE, RTCM33_GRC, and RTCM30_GR
  mountpoints;
- device GNSS position, UTC, fix quality, satellite count, HDOP, altitude, and
  geoid separation are used to generate a live GGA every second; manual rover
  coordinates are not accepted.

The stable `cors/serviceProvider` setting saves the selected provider alongside
the endpoint, port and mountpoint. Explicit **Connect** now saves the account
on the device, binds the saved configuration generation, and starts the
Timesync module's native RTK connection using its own network. Select Timesync
RTK mode first and explicitly consent to sending live GNSS position. The UI
requires fresh status and confirms module stop; opening this page never starts
CORS. The older socket transport remains separately tested, not a silent
fallback when native account saving fails.

Passwords are never written to Viewer logs. Connect explicitly saves the
account on the device. The Viewer-side copy is session-only unless
**Remember password** is explicitly enabled; that option stores the password
in the current user's local Qt settings.

The Viewer does not compile Host SDK sources. The matching binary SDK is
pinned as the `third_party/Prism-SDK` Git submodule.

Viewer 1.3.0 pins the current SDK **1.3.0** across all platforms.
The `sdk-runtime` test checks the actual linked/loaded libraries and all required
extension versions without connecting to hardware.
The SDK release also provides the aligned C++ RK-local API for applications on
the RK device. Viewer uses the Host USB API; updating Viewer does not flash or
restart any connected device. Device firmware must be upgraded separately.
The package contains:

- public headers under `include/prism`;
- `prism_usb_sdk.dll` on Windows, loaded at runtime with `LoadLibraryW` and
  `GetProcAddress` (the import `.lib` is deliberately not used);
- the unified Ubuntu 20.04-baseline `libprism_usb_sdk.so` on Linux x86-64;
- `libprism_usb_sdk.a` on Linux arm64, linked into the Viewer executable;
- `libprism_usb_sdk.dylib` plus its relocatable `libusb` runtime on Apple
  Silicon macOS, linked from the app's `Contents/Frameworks` directory.

The Viewer and SDK submodule versions must match exactly. Windows, Linux, and
macOS builds therefore need no device implementation sources or separately installed
SDK. Clone with submodules enabled:

```sh
git clone --recurse-submodules git@github.com:DIBULI/Prism-Viewer.git
```

For an existing checkout, initialize the pinned SDK with:

```sh
git submodule update --init --recursive
```

## Local Datasets

The automatically selected Prism-415M live mode can record
the original four-camera GPU-compressed RAW12 stream over confirmed USB3.
Select **Record → Four-camera original RAW12** after all four cameras have a
stable input rate. Each recording creates a timestamped subdirectory. The
writer accepts1 or2FPS per camera; it does not change the device's frame rate
or turn a1FPS source into2FPS. The matching 415M capture entry requests2FPS.
The replaceable preview queue is never used as a
recording source. Disk queue overflow drops queued whole frames and reports
the counts. Stop drains the queue and performs full disk readback, decode and
CRC validation before marking the text manifest complete. A write, source
sequence or USB-link error is reported without stopping otherwise healthy
preview streams. The original-RAW12 recording format records cameras only,
not IMU or GNSS; the separate IMU recorder remains available.

Prism-415M RAW12 recordings support four-camera grayscale playback, pause/seek,
full-resolution zoom and full decode/CRC validation. Independent camera counts
and accounted recording drops are preserved. RAW12 diagnostic timestamps are
not exposure UTC. This format currently supports playback, not ROS bag export;
see [dataset format](docs/dataset-format.md).

The RK download dialog uses the currently connected **USB** device and the
SDK's raw-file transfer APIs, not HTTP or port 80. Stop RK capture before
browsing/downloading. SDK downloads preserve original files; optional ROS1/ROS2
bag conversion runs locally in Viewer. No Web address or login is needed.
Use the matching Agent/SDK 1.3.0 build with dataset transfer support; a binary
without the extension reports unsupported, without HTTP fallback.

Recorded **Camera**, **IMU**, and **LiDAR** previews have separate tabs; each
uses the full preview area. Play/pause, speed and the timeline are shared.
Switching tabs does not restart playback or clear recorded IMU/point-cloud
history. IMU-only datasets open on the IMU tab. **Show Metadata** expands
details only when needed.

Datasets can include GNSS/RTK snapshots, raw rover/base RTCM streams and
time-source transitions for offline analysis. See [dataset format](docs/dataset-format.md)
and [ROS bag export](docs/rosbag-export.md) for file and topic details.
ROS1/ROS2 export includes separate GNSS/RTK fixes, native solution state,
satellite counts, and east/north/up precision with ENU covariance when recorded.
Unknown precision stays unknown. Original receiver epochs are retained; GNSS bag
scheduling uses approximate receive time, not a verified measurement timestamp.

## Build

Install Qt Widgets, Qt Network, Qt SQL/SQLite and Zstandard. Qt Charts is not
required. On Ubuntu/Debian:

```sh
sudo apt install build-essential cmake qtbase5-dev libzstd-dev \
  libqt5sql5-sqlite pkg-config libusb-1.0-0-dev libssl-dev
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
scripts\build_viewer_msvc.bat C:\Qt\6.8.3\msvc2022_64
```

On an Apple Silicon Mac, install Qt and run the native build script:

```sh
brew install cmake qtbase qtsvg
./scripts/build_macos.sh
open build-macos/stage/Prism-Viewer.app
```

Set `QT_ROOT` before invoking the script when Qt is installed under a custom
prefix. Windows/macOS scripts download and build the hash-pinned Zstandard
source; no separate Zstandard installation is required. The macOS script
builds and tests arm64, installs the Viewer as a macOS app,
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
git tag v1.3.0
git push origin v1.3.0
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
tar -xzf Prism-Viewer-1.3.0-linux-x64.tar.gz
./Prism-Viewer-1.3.0-linux-x64/bin/prism-viewer

# On an arm64 host:
tar -xzf Prism-Viewer-1.3.0-linux-arm64.tar.gz
./Prism-Viewer-1.3.0-linux-arm64/bin/prism-viewer
```

The Windows x64 release supports Windows 10 version 1809 or newer and Windows
11. Windows ARM64 is not currently supported. The macOS arm64 release targets
macOS 13.0 and supports macOS 13 Ventura, macOS 14 Sonoma, and macOS 15
Sequoia on Apple Silicon; Intel Macs and macOS 12 or earlier are not supported.

Local Linux builds link the Prism SDK statically by default. The release
workflow uses the bundled Ubuntu-20.04-compatible shared SDK for x64 and the
static SDK for arm64. These are not fully static executables: Qt plugins,
libusb and other required runtime dependencies are bundled privately, while
glibc and display drivers come from the host.

## Documentation

- [Independent LiDAR hardware standby / wake](docs/lidar-power.md)
- [Viewer 1.3.0 release notes](docs/release-notes/v1.3.0.md)
- [Viewer 1.3.0 API and behavior changes](docs/update/v1.3.0.md)
- [Viewer 1.2.0 release notes](docs/release-notes/v1.2.0.md)

- [Prism Viewer 1.0.1 update notes](docs/update/v1.0.1.md)
- [Prism Viewer 1.0.1 更新说明](docs/update/v1.0.1.zh-CN.md)
- [Prism Viewer 1.0.0 update notes](docs/update/v1.0.0.md)
- [Prism Viewer 1.0.0 更新说明](docs/update/v1.0.0.zh-CN.md)
- [Prism Viewer 中文用户操作手册](docs/manual/README.md)
- [操作手册生成与截图打码脚本](docs/manual/README.md)
- [Code structure](ARCHITECTURE.md)
- [Dataset directory structure and format](docs/dataset-format.md)
- [ROS1/ROS2 bag export](docs/rosbag-export.md)

## Exposure controls

41M3 retains its existing microsecond/gain controls and synchronous SDK setting
interface. 415M displays integer integration rows, not microseconds or RB delay.
During 415M capture, row changes (3–4096, within device-reported limits) use the
asynchronous API at 1× gain: no STOP/restart, automatic retry, frame discard, or
transition-state labels. Replies are checked while the capture loop continues
to receive image and IMU frames. Unconfirmed changes are reported rather than
assumed successful. Gain, automatic mode, limit editing and explicit refresh
are available only while idle on this path.
The internal adapter preserves each row exactly as ceil(rows × 62.5 us);
41M3 behavior is unchanged. See [API changes](docs/update/v1.3.0.md).

## Recorded dataset playback

When browsing a local dataset, the frame progress slider occupies an
independent full-width row, so changing per-frame exposure text does not resize
it. Use **Go to frame** to enter a 1-based frame number and jump directly to
that camera frame while keeping camera, onboard-IMU, LiDAR, and LiDAR-IMU
playback aligned to its timestamp.
