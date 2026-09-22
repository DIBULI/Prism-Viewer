# GNSS / RTK 天空图与轨迹

Prism Web 和 Prism Viewer 使用 SDK 新增的只读 `gnssObservations(cursor, session)`。
Host USB 和 RK-local C++ 接口同名；不启动采集、CORS，不改变接收机输出配置。
Web 使用 RK-local 补充的独立只读 `prism::rklocal::gnssObservations(cursor, session)`
函数，经 `/run/prism/control.sock` 查询，不调用 `Client::open()`，不占用唯一的
RK-local 采集连接。Host USB 仍调用 `Client::gnssObservations()`；已持有 RK-local
采集连接的应用也可以在原 Client 上调用同名成员函数。此并发旁路仅适用于 RK 本地。
需包含该接口的 Agent/SDK；旧 Agent 返回不支持时界面明确提示，不伪造数据。

## 显示

- 天空图：GSV 方位角、仰角、C/N0；GSA 标识 GNSS 使用卫星，**不代表逐颗 RTK 参与**。
  多信号同一卫星合并，信噪比显示可见信号最大值。缺少方向的卫星只在列表中显示。
  支持旋转、缩放、2D 俯视、左右选择高亮、列表过滤与排序。天空半球表示相对方向，不是实际轨道距离。
- GNSS 轨迹：接收机 GGA，保留其 SINGLE/DGNSS/FLOAT/FIX 类型；它不一定是独立单点解。
- RTK 轨迹：接收机 ADRNAV 原生结果，与 GGA 分开。不使用已移除的 Agent RTKLIB 解算。
  只有有效解才绘图，SINGLE/差分/FLOAT/FIX 明确区分，不把无解坐标当作定位。
- 轨迹使用共用首个有效位置为局部 ENU 原点，单位米；U 需要椭球高（MSL + geoid）。
  不提供高度时仅可在2D显示，U 标为未提供；没有插值、平滑或地图外传。
- 定位超过2秒、天空图超过3秒无更新即视为过期，保留历史轨迹但不冒充当前解。
  断档和 Agent 会话变化不跨段连接；每条显示轨迹最多20000点。清空按钮只清显示历史。
- Web 在 GNSS 页约10Hz轮询；隐藏页面不轮询。显示频率不等于接收机实际输出频率。
  本次新增实时显示，不改变现有数据集格式，也不声称完整记录每个 GNSS 采样。

## 只读协议

请求0x44，24字节：u16 version=1、u16 size=24、u32 reserved=0、u64 cursor、u64 session。
响应0xbb，最大32768字节：32字节头为 version=1、header_size=32、flags（bit0 gap）、
u64 delivered_cursor、u64 RK CLOCK_MONOTONIC毫秒、u64 session。
后续记录：u64 sequence、u64 received_ms、u16 sentence_length、6字节保留0，再接原始ASCII。
仅保留校验正确的 GGA/RMC/GSA/GSV/GST/ADRNAVA；跳过RTCM/Unicore二进制和所有其他回复。
Agent缓存256条，客户端独立游标，慢读者出现gap，不影响UART排空与其他客户端。
session变化必须清空解析状态；position epoch来自接收机，received_ms用于新鲜度，不伪称测量UTC。
没有账号、密码、网络配置、原始观测或任意命令回传。
