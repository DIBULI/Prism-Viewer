# LiDAR hardware standby and wake

In the LiDAR page, use **LiDAR standby / wake**, independently from capture.

1. Connect the device and configure the LiDAR network.
2. Stop all camera, board IMU and LiDAR capture and recording.
3. Select the actual model: MID360, MID360S or PandarXT-32 (not XT32M2X).
4. Click **Read state**, **Standby**, or **Wake**. Confirm changes when prompted.
5. Wait for hardware readback. Motor startup can take more than ten seconds.
6. Start capture separately when wanted.

Normal capture stop does not put the hardware into standby. Wake does not start
capture or recording. A timeout can occur after a command reaches the radar;
read state before retrying. Unknown or unsupported is never reported as standby.
Power controls are disabled during active capture and other device operations.

Use a matched Viewer/SDK/Agent 1.2.0 package containing the power extension.
Earlier builds with the same version string may not implement it. Windows uses
an independent DLL extension without changing the existing RuntimeApi layout.
The UI is available in English and Chinese. No wattage is inferred from mode.
