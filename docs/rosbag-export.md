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
| IMU 0…1 | `/prism/imuN/data` | `sensor_msgs/Imu` | `sensor_msgs/msg/Imu` |
| Mid-360/Mid-360S | `/prism/lidar/points` | `sensor_msgs/PointCloud2` | `sensor_msgs/msg/PointCloud2` |

相机保留原始 JPEG，不做有损重编码。IMU 的加速度和角速度沿用数据集中的
SI 单位（m/s²、rad/s）；设备没有输出姿态，所以
`orientation_covariance[0]` 为 `-1`。点云字段为 `x`、`y`、`z`（float32
米）、`intensity`（float32，来自 Livox reflectivity）和 `tag`（uint8）。

ROS2 消息使用 little-endian CDR 序列化。SQLite 数据库采用 rosbag2 schema
version 3，`metadata.yaml` 采用 version 5，以兼容 ROS2 Humble 及能够读取该
向后兼容格式的更新版本。

## 数据集版本

新录制的数据集格式为 `prism-dataset-v4`。除原有 `imu0.tum`、`imu1.tum`、
四路相机索引和相机容器外，启用 LiDAR 时还会写入：

```text
lidar.tum
lidar-data-0000.bin
lidar-data-0001.bin
...
```

`lidar.tum` 保存接收时间、容器位置、点数、明确选择的型号、Livox 设备类型、
时间类型、批次号和原始时间戳。点坐标在容器中保持毫米整数，只有导出
`PointCloud2` 时才转换成米。

旧的 `prism-dataset-v3` 数据集仍可导出；没有 `lidar.tum` 或没有点云行时，
生成的 bag 只包含四路相机和双 IMU。

Viewer 的“仅录制 IMU”模式生成没有任何 `cam*.tum` 的 v4 数据集。导出器把
四个相机索引全部缺失识别为合法的 IMU-only 数据集，ROS1 和 ROS2 输出都只
创建 `/prism/imu0/data` 与 `/prism/imu1/data` 两个话题；只缺少部分相机索引
仍会被拒绝，避免静默导出不完整的四相机数据。

## 时间戳和文件安全

- 相机与 IMU 使用数据集中已记录的时间戳。
- Livox PTP 时间戳有效时使用原始纳秒时间；其它 Livox 时间模式使用点云批次
  到达 Viewer 时记录的主机时间，避免把 GPS 字节结构误当成 Unix 纳秒。
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
