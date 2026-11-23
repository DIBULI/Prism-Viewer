# MID-360L candidate

The MID-360L changes are offline-qualified and require the corresponding Agent
and Host SDK implementation (Prism model 4, Livox device type 41). The existing
published 1.4.0 SDK submodule pin has not been replaced with a partial runtime
bundle. For development, set `PRISM_USB_SDK_ROOT` to matching candidate headers
and the rebuilt runtime for the target platform. The default published SDK pin
does not yet contain this feature; coordinate all platform SDK artifacts before
publishing this Viewer candidate.

The candidate adds model selection, power query/standby/wake, point/IMU replay,
dataset validation and ROS1/ROS2 bag export. MID-360S speed controls remain
restricted to MID-360S. MID-360L line IDs are unknown, and unsynchronized source
timestamps remain explicitly unsynchronized. Physical streaming, PTP/GPS lock
and long-duration capture remain to be tested with hardware.

Offline tests: `mid-line-dataset`, `lidar-power-panel`, dataset recorder/playback,
bag exporters and SDK-runtime regressions. The new MID-360L fixture covers both
point and IMU records, original timestamps/units and unknown line metadata.
