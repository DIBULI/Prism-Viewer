# Prism Viewer code structure

The Viewer is intentionally split by responsibility. `src/main.cpp` is only
the executable entry point and must stay small.

```text
src/
  main.cpp                         process entry point
  common/
    ui_text.*                      language and Qt/path conversion helpers
  communication/
    device_session.*               USB enumeration, open/close and Client ownership
  control/
    operation_controller.*         lifetime of the single active device task
  transfer/
    camera_frame_assembler.*       VideoChunk -> atomic four-camera frame set
  dataset/
    dataset_browser.*              TUM indexes and camera-container reads
  ui/
    main_window.*                  widgets and high-level user-flow orchestration
    camera_exposure_panel.*        runtime camera exposure controls and state
    camera_zoom_dialog.*           live/dataset full-resolution camera viewer
    preview_image_decoder.*        bounded JPEG decode for live presentation
    wifi_hotspot_panel.*           Wi-Fi hotspot status and idle-only controls
    zoomable_image_view.*          image zoom, pan, fit and 1:1 presentation
  dual_imu_offset_estimator.*      IMU domain algorithm
  imu_timestamp_policy.hpp         IMU timestamp-domain policy
```

## Dependency direction

```text
main
  -> ui
      -> control
      -> communication -> prism_usb_sdk
      -> transfer      -> prism_usb_sdk data types
      -> dataset
      -> IMU algorithms
```

- `main.cpp` does not create protocol objects, streams or worker threads.
- `communication` owns the SDK `Client`. UI code does not enumerate devices
  through platform APIs.
- `control` owns worker-thread lifetime. A new device operation joins the
  previous operation before reusing the worker.
- `transfer` owns stream reassembly and protocol state. The UI receives
  complete frame sets rather than managing per-chunk maps.
- `dataset` owns on-disk format parsing. It has no dependency on window
  widgets.
- `ui` decides which operation the user requested and renders results. It
  should not acquire new binary-protocol parsing responsibilities.

## Where new code goes

| Change | Module |
|---|---|
| USB discovery, device open/close, connection policy | `communication/` |
| Start/stop, mutual exclusion, cancellation, task lifetime | `control/` |
| Camera/IMU frame assembly, stream validation, rate accounting | `transfer/` |
| Local dataset recording, indexing and browsing | `dataset/` |
| Charts, dialogs, tables and window layout | `ui/` |
| Pure calibration or synchronization math | a dedicated algorithm class |

## Wi-Fi hotspot UI

`WifiHotspotPanel` renders the RK Wi-Fi adapter, AP, DHCP, SSID and local
address state. It deliberately does not own a `prism::Client` or create
threads. `MainWindow` serializes refresh/enable/disable calls through the
single `OperationController`, maps `prism::WifiHotspotStatus` into the panel's
view state, and posts results back to the Qt thread.

Wi-Fi hotspot commands are idle-only because command responses and live
camera/IMU frames use the same USB receive endpoint. The Network tab remains
visible but disabled until a USB device is opened, and its controls are locked
while capture, time synchronization, upgrade, or another
hotspot operation is active.

## Camera exposure UI

`CameraExposurePanel` renders the shared automatic-exposure target and the
independent automatic/manual exposure-time mode for all four cameras. It does
not own a `prism::Client` or issue protocol commands. `MainWindow` reads the
runtime configuration after opening a device and serializes refresh/apply
requests. While capture is active, requests are handed to the capture worker
so `readFrame()` and command-response reads never run concurrently.

Exposure settings are runtime-only. The panel does not expose gain because
automatic mode adjusts exposure time only and the runtime exposure protocol
does not contain an AGC field.

`CameraEncodingPanel` renders the persistent MJPEG quality setting in a
separate Encoding tab. It is idle-only and delegates refresh/save transactions
to `MainWindow`; the complete device-configuration read-back is then published
back to the panel.

## Live-view performance policy

Transport correctness is independent from presentation load. Every complete
four-camera frame set is acknowledged and, when recording is active, queued
for disk before the optional preview path:

- the preview queue is latest-wins and never delays USB ACK or recording;
- normal camera tiles use JPEG half-resolution decode, while only the selected
  camera in the live zoom dialog is decoded at full resolution;
- the full-resolution view preserves zoom and pan while live pixmaps are
  replaced, and only recalculates fit-to-window after a resize or image-size
  change;
- camera decode and rendering pause when neither the Camera tab nor its zoom
  dialog is visible, but capture and frame-rate accounting continue;
- IMU table updates are coalesced at 50 Hz; the plot has an independent,
  bounded 100 Hz sample queue and QtCharts redraws at about 30 fps. Both
  queues stop while the IMU tab or main window is hidden;
- the 5-second IMU rate estimate uses 100 ms count anchors instead of storing
  one host timestamp for every sensor sample.

Dataset recording uses Qt's non-native directory picker so third-party
Windows Explorer shell extensions cannot execute inside the Viewer. Filesystem
errors are reported in the UI instead of escaping the Qt callback, the writer
thread has an exception boundary, and its queued JPEG payload is capped at
128 MiB so a slow or removable destination drops frame sets rather than
terminating the process or exhausting host memory.

These presentation policies may skip obsolete preview frames if the host
cannot render them in real time. They do not discard transport frames or
change the reported receive FPS.

Do not add feature implementations to `main.cpp`. If a feature introduces
state and behavior, give it a named class in the matching module and expose
the smallest interface needed by `MainWindow`.
