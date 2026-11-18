# 21312 live IMU reception

The 21312 camera START enables the combined camera and IMU stream. Viewer
correctly skipped a separate IMU START, but selected `AggregateImuObserver`
only for 415. Consequently, 21312 samples were consumed by the inactive
`ImuStream` without invoking the callback. This suppressed both live IMU
display and IMU recording in Viewer. This routing was shared by USB and
network capture; it does not establish that the device stopped producing IMU.

All three products now use `ImuSampleObserver` and the same callback for
parsing, timestamp checks, display and recording. They use one camera+IMU
VIDEO_START/STOP session. Product selection still selects the required
camera format/options. There is no second IMU lifecycle owner.

The old 413 IMU_START was a parameter confirmation, not another hardware
start. Agent `src/control/stream_controller.c` at v1.2.0, v1.3.0 and the
current source enables `imu_enabled` from VIDEO_START and stops the combined
session from VIDEO_STOP. IMU_START requires that session to be already
active. Its sensor-count value only affects the status reply; both physical
IMUs are forwarded by the same session. Viewer no longer sends that
redundant confirmation or relies on its local `active_` flag to deliver data.

The regression uses the actual SDK parser and inactive stream: the old path
consumes a sample without a callback; the production dispatch now delivers
1,000 samples for each product, covering both sensor IDs with the client
closed, rejects malformed samples, and leaves other frames untouched. It
leaves stream ownership to the capture session. The capture option tests verify
the product-specific START requests remain unchanged.

These tests do not claim hardware validation. Live verification receipts are
kept separately under `artifacts/21312-viewer-imu-20261118` in the workspace.
