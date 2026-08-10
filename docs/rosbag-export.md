# Prism 数据集导出 ROS1 / ROS2 Bag

Prism Viewer 的“本地数据集”页面提供“导出 ROS Bag...”菜单。打开一个 Prism
数据集目录后，可以选择：

- **ROS1 Bag (`.bag`)**：单文件、ROS Bag 2.0 chunked 格式。
- **ROS2 Bag (SQLite3)**：一个 `.rosbag2` 目录，其中包含 `metadata.yaml` 和
  `<名称>_0.db3`。

两种转换都由 Viewer 直接完成，不要求电脑安装 ROS、`rosbag`、`ros2` 或
Python 包。ROS2 导出依赖随 Viewer 安装的 Qt SQLite 驱动；Ubuntu/Debian
软件包名为 `libqt5sql5-sqlite`。

## 话题与消息类型

| Prism 数据 | 话题 | ROS1 类型 | ROS2 类型 |
|---|---|---|---|
| Camera 0…3 JPEG | `/prism/cameraN/image/compressed` | `sensor_msgs/CompressedImage` | `sensor_msgs/msg/CompressedImage` |
| 板载 IMU 0 | `/prism/imu0/data` | `sensor_msgs/Imu` | `sensor_msgs/msg/Imu` |
| 板载 IMU 1 | `/prism/imu1/data` | `sensor_msgs/Imu` | `sensor_msgs/msg/Imu` |
| Mid-360/Mid-360S 点云 | `/prism/lidar/points` | `sensor_msgs/PointCloud2` | `sensor_msgs/msg/PointCloud2` |
| Mid-360/Mid-360S 内置 IMU | `/prism/lidar/imu/data` | `sensor_msgs/Imu` | `sensor_msgs/msg/Imu` |

`/prism/imu0/data` 和 `/prism/imu1/data` 始终对应 sensor-board 上的两路板载
IMU；`/prism/lidar/imu/data` 是独立的雷达内置 IMU，不能混入或替代板载 IMU。
三路 IMU 的加速度和角速度都使用数据集中的 SI 单位（m/s²、rad/s）。设备没有
输出姿态，所以 `orientation_covariance[0]` 为 `-1`。

相机保留原始 JPEG，不做有损重编码。点云字段为 `x`、`y`、`z`（float32 米）、
`intensity`（float32，来自 Livox reflectivity）和 `tag`（uint8）。

ROS2 消息使用 little-endian CDR 序列化。SQLite 数据库采用 rosbag2 schema
version 3，`metadata.yaml` 采用 version 5，以兼容 ROS2 Humble 及能够读取该
向后兼容格式的更新版本。

## 数据集版本和录制模式

新录制的数据集格式为 `prism-dataset-v5`。除原有 `imu0.tum`、`imu1.tum`、
四路相机索引和相机容器外，完整模式选择 LiDAR 时还会写入：

```text
lidar.tum
lidar-data-0000.bin
lidar-data-0001.bin
...
lidar_imu.tum
```

`lidar.tum` 保存 UTC 索引时间、容器位置、点数、明确选择的型号、Livox 设备
类型、原始时间类型、批次号和原始时间戳。点坐标在容器中保持毫米整数，只有
导出 `PointCloud2` 时才转换成米。`lidar_imu.tum` 保存雷达内置 IMU 的 SI
数据，以及型号、设备类型、原始时间类型、原始时间戳和同步来源。

录制内容与导出结果如下：

| 模式 | 数据集内容 | 导出的 LiDAR 相关话题 |
|---|---|---|
| 完整 + LiDAR | 四路相机、两路板载 IMU、点云、雷达内置 IMU | `/prism/lidar/points`、`/prism/lidar/imu/data` |
| 仅 IMU + LiDAR | 两路板载 IMU、雷达内置 IMU；无相机、无点云 | 仅 `/prism/lidar/imu/data` |

“仅 IMU + LiDAR”的 bag 因此包含
`/prism/imu0/data`、`/prism/imu1/data`、`/prism/lidar/imu/data` 三个话题，
但不包含相机或点云话题。未选择 LiDAR 的 IMU-only 数据集仍只包含两路板载
IMU 话题。

旧的 `prism-dataset-v3` 和 `prism-dataset-v4` 数据集仍可导出。没有
`lidar_imu.tum` 时不会创建 `/prism/lidar/imu/data`；没有 `lidar.tum` 或没有
点云行时不会创建点云话题。四个相机索引全部缺失会被识别为合法 IMU-only
数据集；只缺少部分相机索引仍会被拒绝，避免静默导出不完整的四相机数据。

## 时间戳和文件安全

- 相机、板载 IMU、LiDAR 点云和雷达 IMU 的 ROS 时间都使用各自索引第一列
  已归一化的 Unix UTC。
- `lidar.tum` 中的 `time_type` 和 `timestamp_raw` 仅用于保留来源。原始 PTP
  纳秒可能处于 TAI 时钟域，导出器不会把它直接用作 ROS 时间。
- `lidar_imu.tum` 中的原始时间和同步标记同样只保留在 Prism 数据集；ROS
  消息时间使用第一列 UTC。
- 转换先写同目录的临时文件或临时目录，完成并封口后才替换已有输出；取消或
  转换失败不会留下一个看似完整的 Bag，也不会破坏原有输出。
- 数据集索引中的容器路径必须是安全的相对路径，拒绝绝对路径和 `..` 跳转。
- 输出路径不能覆盖源 Prism 数据集目录。

## 使用标准 ROS 工具验证

```sh
# ROS1
rosbag info dataset.bag

# ROS2
ros2 bag info dataset.rosbag2
ros2 bag play dataset.rosbag2
```

ROS1 输出使用未压缩 chunk。ROS2 输出使用未压缩 SQLite3，读取端不需要 Prism
专用插件。
