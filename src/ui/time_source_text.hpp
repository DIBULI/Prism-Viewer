#pragma once

#include "common/ui_text.hpp"
#include "communication/device_info_compat.hpp"

namespace prism_viewer::ui {

// Presentation only. Keep raw SDK/dataset provenance unchanged; a Host-set
// UTC epoch still runs on the internal clock, not an ongoing computer source.
inline QString timeSourceText(communication::TimeSyncProvider provider,
                              bool online, bool synchronized) {
  using communication::TimeSyncProvider;
  using common::uiText;
  if (!online) return uiText("Unknown", "未知");
  switch (provider) {
    case TimeSyncProvider::SensorBoardInternal:
    case TimeSyncProvider::RkPtp:
      return uiText("Internal time", "内部时间");
    case TimeSyncProvider::Gps:
      return synchronized ? uiText("External time", "外部时间")
                          : uiText("Internal time", "内部时间");
    case TimeSyncProvider::LegacyUnknown:
      break;
  }
  return uiText("Unknown", "未知");
}

}  // namespace prism_viewer::ui
