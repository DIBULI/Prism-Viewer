# MID-360L support

The MID-360L integration requires the corresponding Agent
and Host SDK implementation (Prism model 4, Livox device type 41). Viewer pins
SDK commit `d15e1bd2426a6d1cd8dbba15ec6ea7c753b8045c`, with matching Windows,
macOS, Linux x64 and Linux ARM64 runtimes built from Agent `404d19a`.

The update adds model selection, power query/standby/wake, point/IMU replay,
dataset validation and ROS1/ROS2 bag export. Speed controls offer normal/low on
MID-360S and normal/low/high on MID-360L, with matched discovery and readback.
MID-360L line IDs are unknown, and unsynchronized source
timestamps remain explicitly unsynchronized.

Hardware streaming and recording were verified on 2026-11-27 with a 413GSV0
and MID-360L firmware 41.52.0.3, using the Agent time-filter startup fix included
in full-r9 and later packages. ROS over USB received 2,900,444 points and 5,786
LiDAR IMU samples in approximately 30 seconds. A separate 30.72-second Web
recording saved 3,073,056 points and 6,148 LiDAR IMU samples; save, download,
STOP and independent sensor-idle checks passed.

These were ROS/USB and Web tests, not a new Viewer GUI hardware regression.
ROS startup synchronization drops, one timestamp-backward warning and four
LiDAR IMU intervals above 7.5 ms were retained in the test evidence. This does
not certify lossless ROS timing, GPS/UTC lock, every power/speed mode or
long-duration stability. The Web recording had no unsynchronized saved samples.

Offline tests: `mid-line-dataset`, `lidar-power-panel`, dataset recorder/playback,
bag exporters and SDK-runtime regressions. The new MID-360L fixture covers both
point and IMU records, original timestamps/units and unknown line metadata.
