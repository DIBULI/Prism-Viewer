#pragma once
#include <QString>
#include <cstdint>
namespace prism_viewer::ui {
// The timing discriminator belongs to the image. A large number is not proof
// of UTC synchronization, and common-trigger timing is not an exposure-end time.
inline QString rawTimingDescription(unsigned mode,uint64_t timestamp,bool chinese) {
  if(mode==0 || (mode==4 && !timestamp))
    return chinese?QStringLiteral("触发时刻未知"):QStringLiteral("Trigger time unknown");
  const auto seconds=QStringLiteral("%1.%2 s")
      .arg(qulonglong(timestamp/1000000)).arg(qulonglong(timestamp%1000000),6,10,QLatin1Char('0'));
  if(mode==4)return (chinese?QStringLiteral("共同外触发时刻 %1（设备时标）"):
      QStringLiteral("Common external trigger %1 (device clock)")).arg(seconds);
  if(mode==3)return (chinese?QStringLiteral("独立诊断时间 %1"):
      QStringLiteral("Independent diagnostic time %1")).arg(seconds);
  return (chinese?QStringLiteral("设备事件时间 %1（时标模式 %2）"):
      QStringLiteral("Device event time %1 (timing mode %2)")).arg(seconds).arg(mode);
}
}
