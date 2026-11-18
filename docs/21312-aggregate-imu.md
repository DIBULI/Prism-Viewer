# 21312 live IMU reception

The 21312 camera START enables the combined camera and IMU stream. Viewer
correctly skipped a separate IMU START, but selected `AggregateImuObserver`
only for 415. Consequently, 21312 samples were consumed by the inactive
`ImuStream` without invoking the callback. This suppressed both live IMU
display and IMU recording in Viewer. This routing was shared by USB and
network capture; it does not establish that the device stopped producing IMU.

All three products now use `ImuSampleObserver` and the same callback for
parsing, timestamp checks, display and recording. Product selection only
controls lifecycle: 415 and 21312 borrow the combined stream, while 413
retains its separate IMU START/STOP. No extra control connection, START or
STOP is introduced.

The regression uses the actual SDK parser and inactive stream: the old path
consumes a sample without a callback; the production dispatch now delivers
1,000 samples for each product, covering both sensor IDs with the client
closed, rejects malformed samples, and leaves other frames untouched. It
also checks that 413 still owns its separate lifecycle. The capture option tests verify
the product-specific START requests remain unchanged.

These tests do not claim hardware validation. Live verification receipts are
kept separately under `artifacts/21312-viewer-imu-20261118` in the workspace.
