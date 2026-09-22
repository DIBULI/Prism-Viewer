# 从 RK 本地磁盘下载原始数据并导出 ROS bag

## 使用

1. RK 通过 `prism-web` 录制到本机磁盘，默认 `/var/lib/prism/recordings/`。
   使用新版录制器后，每次直接生成 **Prism v6 原始数据集**，仍按 CHUNK 缓存写入。
2. 在网页停止并保存，确认采集已停止。
3. Viewer 打开 **本地数据集 → 从 RK 导出……**。
4. 输入 `http://RK的IP:80`，如设备热点 `http://10.42.200.1:80`，点击刷新。
5. 选中数据集、电脑保存目录和输出类型，点击“下载 / 导出”。

输出类型：

- **原始数据集**：逐文件下载，不重新压缩、重采样或改变传感器时间戳。
- **ROS1 (.bag)**：先保存原始目录，再在同级生成 `.bag`。
- **ROS2 (SQLite3)**：先保存原始目录，再在同级生成 `.rosbag2/`。

下载完成后可点击“打开已下载数据集”回放，也可之后使用原有“导出 ROS Bag”按钮。
不需要 USB 采集连接，不会修改相机参数，不会删除 RK 原文件。
地址和保存目录会记住；输出目录冲突时拒绝覆盖。

## 原始文件与时间

`dataset.info` 与 Viewer 原有 v6 格式一致，参见 [数据集格式](dataset-format.md)。
相机 JPEG 保存在 `camera-data-*.bin`；四路相机索引保存公共 TRIG0 时间和实际曝光。
`imu0.tum`、`imu1.tum` 保存板载 IMU 的设备时间及 SI 数据。

选中雷达时，`lidar.tum` 直接索引 `events-data-*.bin` 内的原始 16 字节点数据，
跳过协议头但不重写点；`lidar_imu.tum` 单独保存雷达 IMU。
事件 CHUNK 与 `events.csv` 还保留完整协议 payload 和录制内事件顺序。
不要只复制 `.tum` 或给 CHUNK 改名。

未外部授时时使用 Sensor Board 启动时间（`timestamp_epoch=boot`）；UTC 授时后
使用 Unix 时间。录制中时间域变化会停止并标记未完成，不用下载时间伪造测量戳。
没有同步标记的雷达包保留在原始事件中，并报告跳过计数，不进入 Viewer/ROS 测量索引。
ROS 点云沿用现有标准 `PointCloud2` 及 `offset_time`，聚合和时间单位见
[ROS bag 导出](rosbag-export.md)。不对点云做去畸变。

`manifest.json` 是 Web 录制器附加清单，`viewer_format=prism-dataset-v6` 表示新格式。
旧的 `prism-web-dataset-v1` 目录不会自动改写；没有 v6 清单或未正常结束的记录
可下载原始文件做诊断，但“下载并导出 ROS bag”会拒绝。
没有接入/收到的数据（例如未启用的 CORS、GPS 解算）不会被伪造补齐。

## 下载边界

- 只支持设备 IP 的 HTTP 服务；默认端口 80。PC 与 RK 必须网络互通。
- RK 采集中拒绝下载；Viewer 不会自动停止设备或争抢采集所有权。
- 下载、写盘、bag 导出在工作线程，支持取消。
- 比较文件大小、ETag/If-Match 和前后文件清单；ETag 是文件版本标记，不是 SHA-256。
- 失败/取消保留 `.partial-唯一后缀`，完整下载后才改成正式目录。目前不自动续传。
- 原始数据与 bag 都需要电脑磁盘空间，转换失败时已下载原始目录仍保留。
- **Web 服务未加认证**。同一可达网络的用户可以读取录制文件，文件可能包含位置。
  仅限可信局域网，不应开放公网。

## 验证

新增 `rk-dataset-test` 检查下载字节一致、不覆盖、路径穿越/重定向拒绝、断流、
版本变化和取消。设置 `PRISM_WEB_WORKER` 为服务器编译的 `prism-web-capture` 后，
还会使用真实录制器生成 boot/UTC 两类测试数据，经过 Viewer v6 校验，再分别导出
ROS1 和 ROS2，核对相机、曝光、板载 IMU、雷达点数和雷达 IMU。

```sh
cmake --build build-linux --target prism-viewer rk-dataset-test
PRISM_WEB_WORKER=/work/projects/Prism-agent/build-native/web/prism-web-capture build-linux/rk-dataset-test
```
