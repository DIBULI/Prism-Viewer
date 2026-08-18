# Prism Viewer 数据集格式

Viewer 顶部的“录制...”菜单提供“完整数据集”和“仅录制 IMU”两种模式。
`imu0` 和 `imu1` 始终表示 sensor-board 上的两路板载 IMU；选择
Mid-360/Mid-360S 后，还可以把雷达内置 IMU 单独记录为 `lidar_imu.tum`。

## 结构概览

一个 Prism 数据集是一个必须整体保存和移动的目录，内部文件分为四层：

```text
dataset.info                         录制清单、格式版本、完成状态和计数

cam0.tum ─┐
cam1.tum ─┼──> camera-data-0000.bin 相机索引按偏移和长度引用 JPEG 容器
cam2.tum ─┤        ...
cam3.tum ─┘

lidar.tum ───> lidar-data-0000.bin   点云索引按偏移和长度引用点数据容器

imu0.tum                            直接保存板载 IMU0 的 SI 数据
imu1.tum                            直接保存板载 IMU1 的 SI 数据
lidar_imu.tum                       直接保存可选的雷达内置 IMU SI 数据
```

| 文件 | 作用 | 是否直接包含传感器数据 |
|---|---|---|
| `dataset.info` | 描述版本、录制模式、时间域、存储格式、样本计数和丢弃计数 | 否 |
| `cam0.tum`…`cam3.tum` | 四路相机索引；每行定位一个 JPEG，并保存实际曝光时间 | 否 |
| `camera-data-NNNN.bin` | 多个相机 JPEG 顺序拼接而成的二进制容器 | 是 |
| `imu0.tum`、`imu1.tum` | 两路板载 IMU 的时间戳、加速度和角速度 | 是 |
| `lidar.tum` | LiDAR 点云批次索引及原始时间来源信息 | 否 |
| `lidar-data-NNNN.bin` | 多个笛卡尔点云批次顺序拼接而成的二进制容器 | 是 |
| `lidar_imu.tum` | Mid-360/Mid-360S 内置 IMU 及其时间来源信息 | 是 |

索引中的 `container_path` 是相对于数据集根目录的容器文件名，例如
`camera-data-0000.bin`。`byte_offset` 从容器文件开头以字节计数，
`byte_size` 是该记录占用的字节数。因此不能只复制 `.tum` 文件，也不能单独重命名
容器文件；应始终移动或归档整个数据集目录。以 `#` 开头的 `.tum` 行是注释，
其余行的第一列都是十进制秒时间戳，精度为 1 微秒。

## 当前完整布局（v6）

下面是“完整数据集”并启用 LiDAR 时可能出现的最大文件集合：

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

容器接近 8 GiB 后会按编号创建下一个文件，例如
`camera-data-0001.bin`、`lidar-data-0001.bin`。编号只表示容器分卷顺序，
不表示相机编号或 LiDAR 设备编号。

四种录制组合的实际目录结构如下：

### 完整数据集，不启用 LiDAR

```text
dataset/
├── dataset.info
├── imu0.tum
├── imu1.tum
├── cam0.tum
├── cam1.tum
├── cam2.tum
├── cam3.tum
└── camera-data-0000.bin
```

### 完整数据集，启用 LiDAR

```text
dataset/
├── dataset.info
├── imu0.tum
├── imu1.tum
├── lidar_imu.tum
├── cam0.tum
├── cam1.tum
├── cam2.tum
├── cam3.tum
├── lidar.tum
├── camera-data-0000.bin
└── lidar-data-0000.bin
```

### 仅录制 IMU，不启用 LiDAR

```text
dataset/
├── dataset.info
├── imu0.tum
└── imu1.tum
```

### 仅录制 IMU，启用 LiDAR

```text
dataset/
├── dataset.info
├── imu0.tum
├── imu1.tum
└── lidar_imu.tum
```

不同录制选项对应的内容如下：

| 录制模式 | 未选择 LiDAR | 选择 LiDAR |
|---|---|---|
| 完整数据集 | 四路相机 + 两路板载 IMU | 四路相机 + 两路板载 IMU + 点云 + 雷达内置 IMU |
| 仅录制 IMU | 两路板载 IMU | 两路板载 IMU + 雷达内置 IMU；不录制相机和点云 |

## `dataset.info` 清单

`dataset.info` 是逐行 `key=value` 的 UTF-8 文本文件。下面是完整模式并启用
LiDAR 时的简化示例；实际文件还会包含每路计数和丢弃统计：

```ini
format=prism-dataset-v6
complete=1
recording_mode=full
image_storage=chunk-v1
camera_index=chunk-v2-with-actual-exposure
lidar_storage=cartesian-mm-chunk-v2-with-time-source
lidar_imu_storage=tum-si-v2-with-time-source
time_domain=rk-clock-realtime
timestamp_epoch=unix
timestamp_resolution_us=1
camera_timestamp_reference=trig0-rising-edge
lidar_timestamp_reference=batch-base
point_deskew=none
```

录制开始时先原子写入 `complete=0`；只有停止录制、关闭文件并确认必需数据流
非空后，才会替换为 `complete=1`。因此消费程序应先检查 `format`、
`recording_mode` 和 `complete`，再依据各个 `*_storage` 字段决定哪些可选文件
必须存在。不要仅凭目录中是否碰巧残留某个文件来判断数据集类型。

相机 JPEG 不再分别创建小文件，而是按完整四相机帧集顺序追加到不超过
8 GiB 的容器中。这样可以避免 exFAT 大分配单元导致的空间和写放大，也会显著
减少目录元数据更新。Viewer 仍可读取和导出旧的 v3/v4/v5 数据集；这些版本
没有 `lidar_imu.tum` 时按“未录制雷达 IMU”处理。

v6 的所有数据第一列统一采用设备内部的 RK `CLOCK_REALTIME` 微秒时间轴，其
epoch 按 Unix epoch 表示。这里的“对齐”只要求 Camera、板载 IMU、LiDAR 点云和
雷达 IMU 使用同一设备时间域，不要求 RK 时钟与绝对 UTC 的误差满足某个阈值。
各传感器仍保留原生采样频率，不做重采样、曝光中心补偿或点云运动去畸变。
Viewer 主机时间只用于记录录制作业的开始/结束信息，不生成或后备任何测量戳。

## 板载 IMU

`imu0.tum` 和 `imu1.tum` 分别保存两路板载 IMU，每行格式为：

```text
timestamp_s ax_m_s2 ay_m_s2 az_m_s2 gx_rad_s gy_rad_s gz_rad_s
```

时间戳保留到微秒；加速度单位为 `m/s²`，角速度单位为 `rad/s`。v6 只写入
`timestamp_synced=1` 的板载 IMU 样本，未同步样本只计入 manifest 的
`unsynced_imuN_samples_dropped`。

## 雷达内置 IMU

选择 LiDAR 后，`lidar_imu.tum` 独立保存 Mid-360/Mid-360S 的内置 IMU。v6
写入的每行格式为：

```text
timestamp_s ax_m_s2 ay_m_s2 az_m_s2 gx_rad_s gy_rad_s gz_rad_s model device_type time_type sample_id timestamp_raw timestamp_synced tai_offset_applied
```

- 加速度和角速度已经转换为 SI 单位：`m/s²`、`rad/s`。
- `model` 为 `1`（Mid-360）或 `2`（Mid-360S）；`device_type` 保留 Livox
  上报的设备类型。
- `time_type` 和 `timestamp_raw` 保留雷达原始时钟域及原始纳秒值。原始 PTP
  时间可能采用 TAI，不能直接当作 Unix UTC。
- 第一列 `timestamp_s` 是从雷达测量时间归一化到 RK `CLOCK_REALTIME` 域的值。
  `timestamp_synced=1` 表示来源已同步；`tai_offset_applied=1` 表示转换时应用了
  TAI 到 UTC 的偏移。v6 不把 RK 或 Viewer 的接收时刻伪装成雷达测量时间，
  未同步样本会被丢弃并计入 `unsynced_lidar_imu_samples_dropped`。
- `sample_id` 是雷达 IMU 的独立样本序号，与板载 IMU 序号和点云批次号无关。

导入器继续接受旧数据集的七字段紧凑行和 v5 的六字段来源后缀；v6 录制器始终
写入上述完整来源信息。

## 相机

`cam0.tum` 到 `cam3.tum` 每行固定格式：

```text
timestamp_s container_path byte_offset byte_size actual_exposure_us
```

`byte_offset` 和 `byte_size` 都以字节为单位。索引只会在对应 JPEG 已完整写入
容器后提交。`actual_exposure_us` 是该相机生成这一帧时真正采用的曝光时间，
来自同一 `frame_id` 的 `VIDEO_META.exposure_us[camera]`；PL 自动曝光和手动
曝光模式下都必须位于当前运行时曝光上下限及帧率允许的曝光上限内，不能写入
默认值或 `0`。

四路相机使用同一个帧集时间戳，只采用有效视频元数据中的
`trigger_time_ns / 1000`；该字段是已同步到 RK `CLOCK_REALTIME` epoch 的四路
公共 TRIG0 上升沿，不包含各路曝光中心补偿。PPS/RMC 未同步时 PL
写 0，此时 Viewer 可以继续实时预览，但不会使用 Agent 的单调时钟或 Viewer
到达时间录制该帧。Viewer 只录制四路 JPEG 完整、同帧元数据匹配、四路实际
曝光有效且具有有效 RK epoch 触发时间的帧集。

## LiDAR 点云

`lidar.tum` 每行固定格式：

```text
timestamp_s container_path byte_offset byte_size point_count model device_type time_type batch_id timestamp_raw time_interval_100ns timestamp_synced tai_offset_applied
```

每个点在 `lidar-data-*.bin` 中固定占 16 字节：`x_mm`、`y_mm`、`z_mm` 三个
little-endian int32，随后是 uint8 reflectivity、uint8 tag 和两个保留字节。
`model` 为 `1`（Mid-360）或 `2`（Mid-360S），与用户开始采集时的明确选择
一致。第一列 `timestamp_s` 是 Agent 从该批次的雷达测量时间归一化到 RK
`CLOCK_REALTIME` epoch 的值；`timestamp_raw` 和 `time_type` 保留 Livox 原始
时间来源。
`time_interval_100ns` 保留雷达上报的批内时间间隔字段（单位 0.1 us），
`timestamp_synced` 和 `tai_offset_applied` 说明归一化来源。v6 只写入同步批次。
原始 PTP 可能采用 TAI，因此导出 ROS 时必须使用索引第一列 RK 时间，不能把
原始纳秒直接用作 ROS 时间。
当前 `PointCloud2` 仍以批次基准时间作为 header stamp，未导出逐点时间字段，
所以 `point_deskew=none`。

## 写盘和丢帧

图像与 LiDAR 由独立写盘线程按 RK 测量时间排序处理，避免阻塞 USB 接收线程。待写
图像队列最多保存 256 个完整帧集，LiDAR 队列最多保存 512 批，二者共用
128 MiB 的内存上限。磁盘持续跟不上时，新数据会被丢弃；相机数量记录在
`dropped_frame_sets`，LiDAR 数量记录在 `dropped_lidar_batches` 和
`dropped_lidar_points`。未同步丢弃另由各个 `unsynced_*_dropped` 字段记录，
并在停止录制后的 Viewer 状态和日志中显示。

`dataset.info` 中 `format=prism-dataset-v6`、`image_storage=chunk-v1`、
`camera_index=chunk-v2-with-actual-exposure`、
`lidar_storage=cartesian-mm-chunk-v2-with-time-source` 标识当前容器及索引
格式。雷达 IMU 使用 `lidar_imu_storage=tum-si-v2-with-time-source`，未录制时
为 `none`；`lidar_imu_samples` 记录
实际写入的雷达 IMU 样本数。完整模式未启用 LiDAR 时不会创建空的
`lidar.tum`，且 `lidar_storage`、`lidar_imu_storage` 都标记为 `none`。
`time_domain=rk-clock-realtime`、`timestamp_epoch=unix`、
`timestamp_policy=strict-synchronized-sensor-time` 和
`alignment=common-device-time-domain` 明确时间策略；`recording_host_*` 仅是录制作业的
主机行政时间，不参与数据对齐。录制器在数据文件关闭并检查必需流后，通过临时
文件原子提交 manifest。录制一开始即写入 `complete=0`，停止并成功关闭所有
必需流后才原子替换为 `complete=1`；失败、崩溃或必需流全被丢弃时仍为
`complete=0`。

“仅录制 IMU”不会启动图像/点云写盘线程，也不会创建 `cam*.tum`、
`lidar.tum` 或任何 `.bin` 容器。未选择 LiDAR 时只创建 `imu0.tum`、
`imu1.tum` 和 `dataset.info`；选择 LiDAR 时额外创建 `lidar_imu.tum`。manifest
写入 `recording_mode=imu-only`，并把 `image_storage`、`camera_index` 和
`lidar_storage` 标记为 `none`。在已有完整数据集目录中选择覆盖时，Viewer 会
明确提示并删除旧相机与 LiDAR 索引和容器，避免残留数据被误认为本次录制内容。

## 数据集浏览

Viewer 的“数据集”Tab 可以在不打开 USB 设备的情况下选择上述目录。完整
数据集的时间轴按四路索引中共同存在的完整帧集数量显示；当前帧栏和图像提示
会显示四路实际曝光时间。可以逐帧前后移动，也可以按原始相邻帧时间戳以
0.25x、0.5x、1x、2x、4x 或 8x 速度播放；播放速度只改变浏览节奏，不修改
文件时间戳或 ROS Bag 导出结果。点击任意缩略图可在独立窗口中放大并在四路
相机之间切换。缺少容器偏移、长度或实际曝光字段的数据集会被拒绝。仅 IMU
数据集没有相机时间轴，但仍可查看样本统计并导出 ROS Bag。

“导出 ROS Bag...”可把 v3/v4/v5/v6 数据集转换成标准 ROS1 Bag 或 ROS2
rosbag2 SQLite3 目录；详细映射见 [`rosbag-export.md`](rosbag-export.md)。

Viewer 的“验证数据集...”会在不生成 ROS Bag 的情况下完整检查数据集：v6
manifest 是否已经 `complete=1`、录制模式与必需文件是否一致、四路 Camera
索引是否同帧同时间戳、IMU/LiDAR 行格式与数值是否有效、容器路径和字节范围
是否安全、JPEG 是否能解码，以及 LiDAR 点数是否与二进制长度一致。

验证器还逐路分析时间戳。重复、倒退、超出 v6 声明的 RK
`CLOCK_REALTIME` 时间域，以及超过 1 秒或正常中位周期 100 倍的严重向前跳变
判为错误；相对本流中位周期和正常抖动显著偏大的断流间隔判为警告。报告会给出
文件、行号、前后时间戳、跳变量及每路中位/最小/最大采样间隔。由于各传感器
保留原生采样率，校验器不要求不同数据流落在同一采样时刻，也不会把合法的
1–30 FPS Camera 周期或 IMU/LiDAR 正常抖动误判为跨流不同步。
