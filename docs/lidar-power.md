# LiDAR hardware standby and wake

In the LiDAR page, select the **Hardware** subtab in the left sidebar, then use
**LiDAR standby / wake**, independently from capture. **View**, **Network**,
and **Hardware** have separate scrollable pages so they do not squeeze the point
cloud or each other's controls in smaller windows.

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

## MID-360S motor speed

With capture stopped, select **MID-360S** and click **Read speed**. After a
confirmed read, choose **Normal (high)** or **Low speed** and confirm. The
display shows the last hardware readback, not measured RPM. Other models hide
these controls; actual device identity is also checked before any write.
Older MID-360S firmware may reject the operation and must not be shown as success.

Low speed is not standby and does not change point-rate settings. It does not
guarantee lower vibration on every mounting. The selection is not automatically
saved or reapplied after restart/reconnect; read again. Failed operations clear
the result and require another read. Do not repeatedly click after a timeout.
Use a matching rebuilt SDK/device package containing the speed extension.
