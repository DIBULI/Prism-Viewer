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
gps_rtk.csv                         GPS/RTK 解算与差分链路状态快照
rover_rtcm.bin                      GNSS 接收机上行的原始 RTCM3 字节流
rover_rtcm.csv                      rover 原始流的接收时间与字节范围索引
base_rtcm.bin                       CORS 下发并成功送入 Agent 的原始 RTCM 字节流
base_rtcm.csv                       base 原始流的接收时间与字节范围索引
time_sync.csv                       RK/SensorBoard 时间同步状态快照
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
| `gps_rtk.csv` | GPS/RTK 解算结果、置信度和差分链路状态快照 | 是 |
| `rover_rtcm.bin` | 从设备 GNSS 流中提取并经 CRC 校验的完整 RTCM3 帧；不包含 NMEA | 是 |
| `rover_rtcm.csv` | rover 原始 RTCM 的接收顺序、时间、偏移、长度和 Agent 丢弃计数 | 否 |
| `base_rtcm.bin` | CORS 收到且已经成功送入 Agent 的原始 RTCM2.x/RTCM3.x 字节 | 是 |
| `base_rtcm.csv` | base 原始 RTCM 的接收顺序、时间、偏移和长度 | 否 |
| `time_sync.csv` | 录制期间的 GPS/RK-PTP/未同步状态历史 | 是 |

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
├── gps_rtk.csv
├── rover_rtcm.bin
├── rover_rtcm.csv
├── base_rtcm.bin
├── base_rtcm.csv
├── time_sync.csv
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
├── gps_rtk.csv
├── rover_rtcm.bin
├── rover_rtcm.csv
├── base_rtcm.bin
├── base_rtcm.csv
├── time_sync.csv
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
├── gps_rtk.csv
├── rover_rtcm.bin
├── rover_rtcm.csv
├── base_rtcm.bin
├── base_rtcm.csv
├── time_sync.csv
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
├── imu1.tum
├── gps_rtk.csv
├── rover_rtcm.bin
├── rover_rtcm.csv
├── base_rtcm.bin
├── base_rtcm.csv
└── time_sync.csv
```

### 仅录制 IMU，启用 LiDAR

```text
dataset/
├── dataset.info
├── imu0.tum
├── imu1.tum
├── lidar_imu.tum
├── gps_rtk.csv
├── rover_rtcm.bin
├── rover_rtcm.csv
├── base_rtcm.bin
├── base_rtcm.csv
└── time_sync.csv
```

不同录制选项对应的内容如下：

| 录制模式 | 未选择 LiDAR | 选择 LiDAR |
|---|---|---|
| 完整数据集 | 四路相机 + 两路板载 IMU + 双路原始 RTCM | 四路相机 + 两路板载 IMU + 点云 + 雷达内置 IMU + 双路原始 RTCM |
| 仅录制 IMU | 两路板载 IMU + 双路原始 RTCM | 两路板载 IMU + 雷达内置 IMU + 双路原始 RTCM；不录制相机和点云 |

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
gps_rtk_storage=csv-v1
rover_rtcm_storage=raw-rtcm3-v1
rover_rtcm_index=csv-v1
base_rtcm_storage=raw-rtcm-v1
base_rtcm_index=csv-v1
time_sync_storage=csv-v1
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

v6 的所有数据第一列统一采用录制开始时选定的公共传感器微秒时间轴。

实际录制会在开始时锁定以下两种时间域之一：

- Sensor Board 尚未锁定外部 PPS/NMEA 时，使用
  `time_domain=sensor-board-clock`、`timestamp_epoch=boot`。时间从 Sensor
  Board 启动时的 0 开始，所有传感器仍处于同一内部时间轴。
- 外部时间已经锁定时，使用
  `time_domain=rk-clock-realtime`、`timestamp_epoch=unix`，时间戳表示 UTC。

同一次录制不允许在这两种 epoch 之间切换；若外部时间在录制过程中锁定或丢失，
Viewer 会结束为失败并提示重新开始录制，避免一个数据集内出现巨大时间跳变。
这里的“对齐”只要求 Camera、板载 IMU、LiDAR 点云和雷达 IMU 使用同一设备时间域。
各传感器仍保留原生采样频率，不做重采样、曝光中心补偿或点云运动去畸变。
Viewer 主机时间只用于记录录制作业的开始/结束信息，不生成或后备任何测量戳。

## 原始 RTCM

录制器保存两路彼此独立、可直接重新送入解码器的二进制流：

- `rover_rtcm.bin` 是 Sensor Board 所接 GNSS 接收机的上行数据。Agent 从混合的
  NMEA/RTCM 输入中只提取具有正确 RTCM3 帧长和 CRC24Q 的完整帧，因此该文件
  不包含 NMEA，也不包含损坏或不完整帧。
- `base_rtcm.bin` 是 Viewer 从 CORS/NTRIP 收到并已成功通过 SDK 送入 Agent 的
  原始字节，保持服务端 RTCM2.x 或 RTCM3.x 格式，不做重编码、拆帧或合并。

二进制文件只按接收顺序拼接原始字节。对应 CSV 用于按录制时序重放：

```text
# rover_rtcm.csv
batch_index,recording_elapsed_us,host_receive_unix_us,byte_offset,byte_size,stream_sequence,agent_dropped_bytes

# base_rtcm.csv
batch_index,recording_elapsed_us,host_receive_unix_us,byte_offset,byte_size
```

`recording_elapsed_us` 来自 Viewer 单调时钟，是回放时恢复每批输入节奏的依据；
`host_receive_unix_us` 仅用于诊断。`byte_offset`/`byte_size` 定位 `.bin` 中的原始
字节。`stream_sequence` 是 Agent 的 USB 事件序号，`agent_dropped_bytes` 是自本次
订阅开始累计的丢弃字节数。录制结束时该值非零会使数据集保持
`complete=0`，避免把缺失 rover 输入的数据集用于算法复现。

两路 RTCM 文件在没有 GNSS 或未连接 CORS 时允许为空；这表示录制期间确实没有
对应输入，不会影响 Camera、IMU 或 LiDAR 数据集本身的有效性。`gps_rtk.csv`
继续保存当时 Agent 的解算输出，可用于和原始 RTCM 重放后的新算法结果做对照。

## 时间同步状态

`time_sync.csv` 在录制开始时立即写入一条状态，此后随 Viewer 对
`DeviceInfo` 的周期刷新继续记录（正常采集约 1 Hz）。每行字段为：

```text
sample_index,recording_elapsed_us,host_receive_unix_us,rk_system_time_us,device_info_version,sensor_board_online,sensor_board_time_synced,time_sync_provider,time_sync_provider_name,imu_time_synced_mask,sensor_board_error_flags
```

`recording_elapsed_us` 使用 Viewer 单调时钟，因此即使主机或 RK 系统时间发生
校时跳变，也能保持状态观测顺序；`host_receive_unix_us` 是 Viewer 收到状态时的
主机 UTC，`rk_system_time_us` 是最近一个 RK heartbeat 报告的当前设备时间，
尚未收到 heartbeat 时允许为 0。在 `boot` epoch 下它是启动相对时间，在
`unix` epoch 下它是 UTC。`time_sync_provider` 的值为 `0`（Sensor Board 内部
启动时间）、`1`（Host UTC 经 Sensor Board）、`2`（外部 GPS/RTK）或 `255`
（旧版 Agent 只能确认已同步、无法报告来源）。

`sensor_board_time_synced` 表示 Sensor Board 的公共传感器时间轴是否可用；新版
固件从启动起即为 1。是否锁定外部 UTC 由 `time_sync_provider=2` 单独表示。
`imu_time_synced_mask` 的 bit0/bit1 分别表示板载 IMU0/IMU1 是否使用同步时间。
`dataset.info` 中的 `time_sync_samples` 保存快照数量，
`time_sync_provider_transitions` 保存录制期间观测到的来源切换次数。时间同步状态
是独立状态流，不会被重复写入每一行高频 IMU 或相机索引。

## 板载 IMU

`imu0.tum` 和 `imu1.tum` 分别保存两路板载 IMU，每行格式为：

```text
timestamp_s ax_m_s2 ay_m_s2 az_m_s2 gx_rad_s gy_rad_s gz_rad_s
```

时间戳保留到微秒；加速度单位为 `m/s²`，角速度单位为 `rad/s`。v6 只写入
`timestamp_synced=1` 的板载 IMU 样本，未同步样本只计入 manifest 的
`unsynced_imuN_samples_dropped`。IMU0、IMU1 至少一路同步即可开始并完成录制；
未同步通道的索引文件可以为空，但两路都为空的数据集无效。

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
- 第一列 `timestamp_s` 是从雷达测量时间归一化到 manifest 声明的公共传感器
  时间域的值。
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
`trigger_time_ns / 1000`；该字段是 Sensor Board 公共时间轴上的四路公共 TRIG0
上升沿，不包含各路曝光中心补偿。没有外部 PPS/NMEA 时它仍使用 Sensor Board
启动时间，不会退回 Agent 单调时钟或 Viewer 到达时间。Viewer 只录制四路 JPEG
完整、同帧元数据匹配、四路实际曝光有效且具有有效公共时间戳的帧集。

## LiDAR 点云

`lidar.tum` 每行固定格式：

```text
timestamp_s container_path byte_offset byte_size point_count model device_type time_type batch_id timestamp_raw time_interval_100ns timestamp_synced tai_offset_applied
```

每个点在 `lidar-data-*.bin` 中固定占 16 字节：`x_mm`、`y_mm`、`z_mm` 三个
little-endian int32，随后是 uint8 reflectivity、uint8 tag 和两个保留字节。
`model` 为 `1`（Mid-360）或 `2`（Mid-360S），与用户开始采集时的明确选择
一致。第一列 `timestamp_s` 是 Agent 从该批次的雷达测量时间归一化到 manifest
声明的公共传感器时间域的值；`timestamp_raw` 和 `time_type` 保留 Livox 原始
时间来源。
`time_interval_100ns` 保留雷达上报的批内时间间隔字段（单位 0.1 us），
`timestamp_synced` 和 `tai_offset_applied` 说明归一化来源。v6 只写入同步批次。
原始 PTP 可能采用 TAI，因此导出 ROS 时必须使用索引第一列 RK 时间，不能把
原始纳秒直接用作 ROS 时间。
导出 ROS Bag 时，Viewer 使用批次基准时间、`time_interval_100ns` 和点序号恢复
每点时间，并按 Livox ROS Driver 2 默认的 10 Hz（100 ms）窗口聚合为
`sensor_msgs/PointCloud2`。header stamp 是帧内第一点的 Unix epoch 纳秒时间，
每个点的 uint32 `offset_time` 字段是相对该基准的纳秒偏移；最后不足 100 ms 的
尾帧也会输出。这里只恢复时间，不对坐标做运动去畸变，所以
`point_deskew=none`。

## 写盘和丢帧

图像与 LiDAR 由独立写盘线程按公共传感器时间排序处理，避免阻塞 USB 接收线程。待写
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
`time_domain`/`timestamp_epoch` 使用 `rk-clock-realtime`/`unix` 或
`sensor-board-clock`/`boot` 配对，
`timestamp_policy=strict-synchronized-sensor-time` 和
`alignment=common-device-time-domain` 明确时间策略；`recording_host_*` 仅是录制作业的
主机行政时间，不参与数据对齐。录制器在数据文件关闭并检查必需流后，通过临时
文件原子提交 manifest。录制一开始即写入 `complete=0`，停止并成功关闭所有
必需流后才原子替换为 `complete=1`；失败、崩溃或必需流全被丢弃时仍为
`complete=0`。

“仅录制 IMU”不会启动图像/点云写盘线程，也不会创建 `cam*.tum`、
`lidar.tum` 或相机/点云容器。原始 RTCM 文件与录制模式无关，完整模式和
“仅录制 IMU”都会创建 `rover_rtcm.bin/.csv`、`base_rtcm.bin/.csv`。未选择
LiDAR 时还会创建 `imu0.tum`、`imu1.tum`、`gps_rtk.csv`、`time_sync.csv` 和
`dataset.info`；选择 LiDAR 时
额外创建 `lidar_imu.tum`。manifest
写入 `recording_mode=imu-only`，并把 `image_storage`、`camera_index` 和
`lidar_storage` 标记为 `none`。在已有完整数据集目录中选择覆盖时，Viewer 会
明确提示并删除旧相机与 LiDAR 索引和容器，避免残留数据被误认为本次录制内容。

## 数据集浏览

Viewer 的“数据集”Tab 可以在不打开 USB 设备的情况下选择上述目录。完整
数据集会把四路相机完整帧集、`imu0.tum`、`imu1.tum`、`lidar.tum` 和
`lidar_imu.tum` 合并为同一条按测量时间戳排序的回放时间线。播放时，相机帧显示
在“数据集”页，两路板载 IMU 同步更新“IMU”页的数值表和曲线，LiDAR 点批次
同步送入“雷达”页的点云视图，雷达内置 IMU 的当前 SI 数值也显示在该页。
因此回放数据集时无需打开 USB 设备。

当前帧栏和图像提示会显示四路实际曝光时间。可以逐帧前后移动，也可以让完整
传感器时间线按原始时间戳以 0.25x、0.5x、1x、2x、4x 或 8x 速度播放；拖动
相机帧滑块时，IMU、点云和雷达 IMU 会一起定位到该帧时间。播放速度只改变
回放节奏，不修改文件时间戳或 ROS Bag 导出结果。点击任意缩略图可在独立窗口
中放大并在四路相机之间切换。缺少容器偏移、长度或实际曝光字段的数据集会被
拒绝。仅 IMU 或无相机数据集也能直接播放其传感器时间线，并继续支持 ROS Bag
导出。

“导出 ROS Bag...”可把 v3/v4/v5/v6 数据集转换成标准 ROS1 Bag 或 ROS2
rosbag2 SQLite3 目录；详细映射见 [`rosbag-export.md`](rosbag-export.md)。

Viewer 的“验证数据集...”会在不生成 ROS Bag 的情况下完整检查数据集：v6
manifest 是否已经 `complete=1`、录制模式与必需文件是否一致、四路 Camera
索引是否同帧同时间戳、IMU/LiDAR 行格式与数值是否有效、容器路径和字节范围
是否安全、JPEG 是否能解码、LiDAR 点数是否与二进制长度一致，以及两路原始
RTCM 索引是否无缺口覆盖整个二进制文件。rover 事件序号、累计丢弃字节或索引
偏移不连续都会判为错误。

验证器还逐路分析时间戳。重复、倒退、超出 v6 声明的 RK
`CLOCK_REALTIME` 时间域，以及超过 1 秒或正常中位周期 100 倍的严重向前跳变
判为错误；相对本流中位周期和正常抖动显著偏大的断流间隔判为警告。报告会给出
文件、行号、前后时间戳、跳变量及每路中位/最小/最大采样间隔。由于各传感器
保留原生采样率，校验器不要求不同数据流落在同一采样时刻，也不会把合法的
1–30 FPS Camera 周期或 IMU/LiDAR 正常抖动误判为跨流不同步。
