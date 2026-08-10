# Prism Viewer 数据集格式

Viewer 顶部的“录制...”菜单提供“完整数据集”和“仅录制 IMU”两种模式。
`imu0` 和 `imu1` 始终表示 sensor-board 上的两路板载 IMU；选择
Mid-360/Mid-360S 后，还可以把雷达内置 IMU 单独记录为 `lidar_imu.tum`。

## 当前布局（v5）

```text
dataset/
├── dataset.info
├── imu0.tum
├── imu1.tum
├── lidar_imu.tum          # 可选：Mid-360/Mid-360S 内置 IMU
├── cam0.tum
├── cam1.tum
├── cam2.tum
├── cam3.tum
├── lidar.tum              # 可选：LiDAR 点云索引
├── camera-data-0000.bin
├── camera-data-0001.bin   # 数据超过 8 GiB 时自动创建
└── lidar-data-0000.bin    # 仅完整模式并启用 LiDAR 时包含点数据
```

不同录制选项对应的内容如下：

| 录制模式 | 未选择 LiDAR | 选择 LiDAR |
|---|---|---|
| 完整数据集 | 四路相机 + 两路板载 IMU | 四路相机 + 两路板载 IMU + 点云 + 雷达内置 IMU |
| 仅录制 IMU | 两路板载 IMU | 两路板载 IMU + 雷达内置 IMU；不录制相机和点云 |

相机 JPEG 不再分别创建小文件，而是按完整四相机帧集顺序追加到不超过
8 GiB 的容器中。这样可以避免 exFAT 大分配单元导致的空间和写放大，也会显著
减少目录元数据更新。Viewer 仍可读取和导出旧的 v3/v4 数据集；这些版本没有
`lidar_imu.tum` 时按“未录制雷达 IMU”处理。

## 板载 IMU

`imu0.tum` 和 `imu1.tum` 分别保存两路板载 IMU，每行格式为：

```text
timestamp_s ax_m_s2 ay_m_s2 az_m_s2 gx_rad_s gy_rad_s gz_rad_s
```

时间戳保留到微秒；加速度单位为 `m/s²`，角速度单位为 `rad/s`。

## 雷达内置 IMU

选择 LiDAR 后，`lidar_imu.tum` 独立保存 Mid-360/Mid-360S 的内置 IMU。v5
写入的每行格式为：

```text
timestamp_s ax_m_s2 ay_m_s2 az_m_s2 gx_rad_s gy_rad_s gz_rad_s model device_type time_type sample_id timestamp_raw timestamp_synced
```

- 加速度和角速度已经转换为 SI 单位：`m/s²`、`rad/s`。
- `model` 为 `1`（Mid-360）或 `2`（Mid-360S）；`device_type` 保留 Livox
  上报的设备类型。
- `time_type` 和 `timestamp_raw` 保留雷达原始时钟域及原始纳秒值。原始 PTP
  时间可能采用 TAI，不能直接当作 Unix UTC。
- 第一列 `timestamp_s` 是归一化后的 Unix UTC。`timestamp_synced=1` 表示它
  来自已同步的雷达时间（必要时已应用 TAI 到 UTC 的偏移）；值为 `0` 时使用
  RK `CLOCK_REALTIME` 的接收时间作为后备。
- `sample_id` 是雷达 IMU 的独立样本序号，与板载 IMU 序号和点云批次号无关。

导入器也接受只有前七个字段的紧凑 LiDAR IMU 行，便于兼容早期测试数据；v5
录制器始终写入上述完整来源信息。

## 相机

`cam0.tum` 到 `cam3.tum` 每行固定格式：

```text
timestamp_s container_path byte_offset byte_size actual_exposure_us
```

`byte_offset` 和 `byte_size` 都以字节为单位。索引只会在对应 JPEG 已完整写入
容器后提交。`actual_exposure_us` 是该相机生成这一帧时真正采用的曝光时间，
来自同一 `frame_id` 的 `VIDEO_META.exposure_us[camera]`；PL 自动曝光和手动
曝光模式下都必须为 `200..15000` 微秒，不能写入默认值或 `0`。

四路相机使用同一个帧集时间戳。优先采用有效视频元数据中的
`trigger_time_ns / 1000`；该字段是四路公共 TRIG0 上升沿的 Unix UTC，不包含
各路曝光中心补偿。PPS/RMC 未同步时 PL 写 0，Viewer 才使用接收链路提供的后备
时间。Viewer 只提交并录制四路 JPEG 完整、同帧元数据匹配且四路实际曝光均
有效的帧集。

## LiDAR 点云

`lidar.tum` 每行固定格式：

```text
timestamp_s container_path byte_offset byte_size point_count model device_type time_type batch_id timestamp_raw
```

每个点在 `lidar-data-*.bin` 中固定占 16 字节：`x_mm`、`y_mm`、`z_mm` 三个
little-endian int32，随后是 uint8 reflectivity、uint8 tag 和两个保留字节。
`model` 为 `1`（Mid-360）或 `2`（Mid-360S），与用户开始采集时的明确选择
一致。第一列 `timestamp_s` 是归一化的 Unix UTC；`timestamp_raw` 和
`time_type` 只保留 Livox 原始时间来源。原始 PTP 可能采用 TAI，因此导出 ROS
时必须使用索引第一列 UTC，不能把原始 PTP/TAI 纳秒直接用作 ROS 时间。

## 写盘和丢帧

图像与 LiDAR 由独立写盘线程按接收时间处理，避免阻塞 USB 接收线程。待写
图像队列最多保存 256 个完整帧集，LiDAR 队列最多保存 512 批，二者共用
128 MiB 的内存上限。磁盘持续跟不上时，新数据会被丢弃；相机数量记录在
`dropped_frame_sets`，LiDAR 数量记录在 `dropped_lidar_batches` 和
`dropped_lidar_points`，并在停止录制后的 Viewer 状态中显示。

`dataset.info` 中 `format=prism-dataset-v5`、`image_storage=chunk-v1`、
`camera_index=chunk-v2-with-actual-exposure`、
`lidar_storage=cartesian-mm-chunk-v1` 标识当前容器及索引格式。雷达 IMU 使用
`lidar_imu_storage=tum-si-v1`，未录制时为 `none`；`lidar_imu_samples` 记录
实际写入的雷达 IMU 样本数。完整模式未启用 LiDAR 时不会创建空的
`lidar.tum`，且 `lidar_storage`、`lidar_imu_storage` 都标记为 `none`。

“仅录制 IMU”不会启动图像/点云写盘线程，也不会创建 `cam*.tum`、
`lidar.tum` 或任何 `.bin` 容器。未选择 LiDAR 时只创建 `imu0.tum`、
`imu1.tum` 和 `dataset.info`；选择 LiDAR 时额外创建 `lidar_imu.tum`。manifest
写入 `recording_mode=imu-only`，并把 `image_storage`、`camera_index` 和
`lidar_storage` 标记为 `none`。在已有完整数据集目录中选择覆盖时，Viewer 会
明确提示并删除旧相机与 LiDAR 索引和容器，避免残留数据被误认为本次录制内容。

## 数据集浏览

Viewer 的“数据集”Tab 可以在不打开 USB 设备的情况下选择上述目录。完整
数据集的时间轴按四路索引中共同存在的完整帧集数量显示；当前帧栏和图像提示
会显示四路实际曝光时间。点击任意缩略图可在独立窗口中放大并在四路相机之间
切换。缺少容器偏移、长度或实际曝光字段的数据集会被拒绝。仅 IMU 数据集没有
相机时间轴，但仍可查看样本统计并导出 ROS Bag。

“导出 ROS Bag...”可把 v3/v4/v5 数据集转换成标准 ROS1 Bag 或 ROS2
rosbag2 SQLite3 目录；详细映射见 [`rosbag-export.md`](rosbag-export.md)。
