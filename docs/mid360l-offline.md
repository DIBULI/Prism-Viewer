# MID-360L support

The MID-360L changes are offline-qualified and require the corresponding Agent
and Host SDK implementation (Prism model 4, Livox device type 41). Viewer pins
SDK commit `d15e1bd2426a6d1cd8dbba15ec6ea7c753b8045c`, with matching Windows,
macOS, Linux x64 and Linux ARM64 runtimes built from Agent `404d19a`.

The update adds model selection, power query/standby/wake, point/IMU replay,
dataset validation and ROS1/ROS2 bag export. Speed controls offer normal/low on
MID-360S and normal/low/high on MID-360L, with matched discovery and readback.
MID-360L line IDs are unknown, and unsynchronized source
timestamps remain explicitly unsynchronized. Physical streaming, PTP/GPS lock
and long-duration capture remain to be tested with hardware.

Offline tests: `mid-line-dataset`, `lidar-power-panel`, dataset recorder/playback,
bag exporters and SDK-runtime regressions. The new MID-360L fixture covers both
point and IMU records, original timestamps/units and unknown line metadata.
