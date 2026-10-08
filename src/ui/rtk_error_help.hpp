#pragma once

#include "common/ui_text.hpp"
#include <QtCore/QString>
#include <cstdint>
#include <optional>

namespace prism_viewer::ui {
// These are device wire codes. Do not use the host platform's strerror/errno.
struct RtkErrorEntry { const char* kind; int code; const char* zh; const char* en; };
inline constexpr RtkErrorEntry rtkErrorEntries[] = {
  {"port", 0, "接口未报告错误；这不代表 GNSS 已定位或 CORS 已连接。", "No port error reported; this does not confirm a GNSS fix or a CORS connection."},
  {"port", -11, "接口实际模式尚未确认与已保存模式一致。停止采集后读取实际状态；确认连接正确后再应用模式。", "The applied port mode is not confirmed to match the saved mode. Stop capture, read status, then apply the mode after checking the connection."},
  {"port", -19, "设备未在线，无法确认接口模式。检查设备供电和连接，恢复后读取实际状态。", "The device is offline, so its port mode cannot be confirmed. Check power and connection, then read status again."},
  {"module", 0, "没有已记录的通信或操作错误；不代表 RTK 已启动、已有定位或 CORS 已连通。", "No recorded communication or operation error; this does not confirm RTK running, a position fix or CORS connectivity."},
  {"module", -1, "位置发送授权或已保存配置与当前配置不一致。刷新账号配置，核对后重新确认授权并启动。", "Position-sharing consent or the saved/current configuration does not match. Refresh the configuration, verify it, then confirm consent and start."},
  {"module", -2, "所需配置或文件不存在。重新读取配置，确认账号已保存；仍有问题时检查设备日志。", "A required configuration or file is missing. Read the configuration and confirm it is saved; check device logs if this persists."},
  {"module", -5, "设备通信或读写失败，操作结果未确认。检查连接并读取实际状态，不要连续重复下发。", "Device communication or I/O failed; the result is unconfirmed. Check the connection and read status before sending another command."},
  {"module", -11, "接收机初始化或数据就绪检查未通过，可能缺少有效时间或完整报文。查看 GNSS 定位与数据新鲜度；就绪后重新应用模式，再单独启动 RTK / CORS。不能仅凭此码判定未定位。", "Receiver initialization or data-readiness checks have not passed; valid time or complete messages may be missing. Check GNSS fix and data freshness. Once ready, reapply the mode and start RTK / CORS separately. This code alone does not prove a missing fix."},
  {"module", -12, "设备内存不足。停止不必要的任务并检查设备日志。", "Device memory is insufficient. Stop unnecessary tasks and check device logs."},
  {"module", -13, "设备访问权限不足。检查设备服务和配置文件权限。", "Device access was denied. Check service and configuration-file permissions."},
  {"module", -16, "设备正在执行其他操作或启停切换。等待当前操作结束，刷新状态后再试。", "Another operation or start/stop transition is in progress. Wait for it to finish, then refresh status before retrying."},
  {"module", -19, "设备或模块不可用。检查供电和连接，恢复后重新读取状态。", "The device or module is unavailable. Check power and connection, then read status again."},
  {"module", -22, "请求参数或配置格式无效。核对服务器地址、端口、挂载点和客户端版本。", "The request or configuration format is invalid. Check server address, port, mountpoint and client version."},
  {"module", -28, "设备存储空间不足，配置可能无法保存。检查剩余空间后重新读取配置。", "Device storage is full and configuration may not be saved. Check free space, then read the configuration again."},
  {"module", -30, "设备存储为只读，配置无法写入。检查文件系统状态。", "Device storage is read-only, so configuration cannot be written. Check filesystem health."},
  {"module", -61, "设备未提供所需状态数据。等待连接恢复后重新读取。", "Required device status data is unavailable. Wait for the connection to recover, then read again."},
  {"module", -71, "收到不符合预期格式的响应。检查客户端与设备版本是否匹配，并重新读取状态。", "The response format is unexpected. Check client/device version compatibility and read status again."},
  {"module", -74, "收到的响应校验失败。检查连接稳定性；若反复出现，保留错误码并检查设备日志。", "Response validation failed. Check connection stability; if repeated, keep the code and inspect device logs."},
  {"module", -75, "计数或数据范围超限，无法安全继续操作。重新读取状态并检查设备日志。", "A counter or data limit was exceeded, preventing safe continuation. Read status and check device logs."},
  {"module", -95, "当前设备固件不支持此功能。使用匹配的客户端与设备固件。", "The device firmware does not support this function. Use matching client and device firmware versions."},
  {"module", -107, "模块连接已断开。检查连接；设备空闲时重新选择 RTK 模式并读取状态。", "The module link is disconnected. Check the connection; while idle, reselect RTK mode and read status."},
  {"module", -110, "操作超时，是否执行成功尚未确认。先读取实际状态，不要直接认定失败或连续重试。", "The operation timed out and completion is unconfirmed. Read actual status before assuming failure or retrying."},
  {"module", -116, "配置或控制状态在操作期间已变化，原结果已失效。刷新状态和配置后再操作。", "Configuration or control state changed during the operation, invalidating the previous result. Refresh status and configuration first."},
  {"module", -121, "模块拒绝命令或报告执行失败。查看 RTK 控制错误与当前状态，排除原因后再试。", "The module rejected the command or reported failure. Check the RTK control error and current status before retrying."},
  {"module", -125, "操作已取消或服务正在退出。待设备恢复稳定后重新读取状态。", "The operation was cancelled or the service is shutting down. Read status after the device stabilizes."},
  {"control", 0, "没有已记录的 RTK 控制错误；仍需查看运行状态，不代表已得到 FLOAT / FIX。", "No recorded RTK control error; check the run state separately. This does not confirm a FLOAT / FIX solution."},
  {"control", 1, "接收机未在期限内确认启停命令。读取状态并检查 GNSS 数据是否持续更新后再试。", "The receiver did not confirm start/stop before the deadline. Read status and check that GNSS data is updating before retrying."},
  {"control", 2, "接收机明确拒绝 RTK 控制命令。检查初始化状态和固件兼容性。", "The receiver explicitly rejected the RTK control command. Check initialization status and firmware compatibility."},
  {"control", 3, "接收机通信中断或重新初始化，之前的启停确认已失效。恢复数据后重新读取并确认状态。", "Receiver communication was lost or reinitialized, invalidating the previous start/stop confirmation. Restore data and read status again."},
  {"control", 4, "启动过程中 CORS 配置发生变化，原启动授权不再适用。刷新并核对账号配置后重新启动。", "CORS configuration changed during startup, invalidating the previous authorization. Refresh and verify the configuration before starting again."},
};
inline QString rtkErrorTitle(const QString& kind) {
  using common::uiText;
  if (kind == "port") return uiText("Device port error", "设备接口错误");
  if (kind == "module") return uiText("RTK-module communication / operation error", "RTK-module 通信 / 操作错误");
  if (kind == "control") return uiText("RTK control error", "RTK 控制错误");
  return uiText("RTCM cumulative error count", "RTCM 错误累计数");
}
inline QString rtkErrorExplanation(const QString& kind, std::optional<int64_t> value) {
  using common::uiText;
  if (!value) return uiText("Not provided; do not interpret this as zero or healthy.", "尚未提供；不能按 0 或正常处理。");
  if (kind == "rtcm") return uiText(
      "Cumulative RTCM header or CRC-validation failures, not an error code or a count of failed RTK solutions. A nonzero value may be historical; growth across readings indicates recent errors. It may reset after a device restart.",
      "累计的 RTCM 帧头异常或 CRC 校验失败次数，不是错误码，也不是 RTK 解算失败次数。非零可能是历史错误；连续读取时仍增长才表示近期仍有异常。计数可在设备重启后重置。");
  for (const auto& entry : rtkErrorEntries)
    if (kind == QLatin1String(entry.kind) && *value == entry.code) return uiText(entry.en, entry.zh);
  return uiText("Unrecognized error code. Keep the original value, read status and check client/device versions; do not infer a cause from this code.",
                "未收录的错误码。保留原值，重新读取状态并核对客户端 / 设备版本；不要据此猜测原因。");
}
inline QString rtkErrorSection(const QString& kind, std::optional<int64_t> value, bool fresh) {
  using common::uiText;
  QString html = "<h3>" + rtkErrorTitle(kind).toHtmlEscaped() + " · " + (value ? QString::number(*value) : QStringLiteral("—")) + "</h3><p>";
  if (!fresh) html += uiText("[Not live / unconfirmed] Refresh status first. ", "【非实时 / 未确认】请先刷新状态。").toHtmlEscaped();
  html += rtkErrorExplanation(kind, value).toHtmlEscaped() + "</p>";
  return html;
}
inline QString rtkErrorReference() {
  using common::uiText;
  QString html;
  for (const char* kind : {"port", "module", "control"}) {
    html += "<h3>" + rtkErrorTitle(QLatin1String(kind)).toHtmlEscaped() + "</h3>";
    for (const auto& entry : rtkErrorEntries)
      if (QString::fromLatin1(kind) == QLatin1String(entry.kind))
        html += "<p><b>" + QString::number(entry.code) + "</b> · " + uiText(entry.en, entry.zh).toHtmlEscaped() + "</p>";
  }
  return html;
}
}  // namespace prism_viewer::ui
