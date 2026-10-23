#pragma once
#include "common/ui_text.hpp"
#include <array>
#include <cstdint>
#include <numeric>
#include <QStringList>

namespace prism_viewer::ui {
struct RecordingCameraLoss {
  std::array<uint64_t,4> images{};
  uint64_t other_sets=0, partial_sets=0;
  QString warning() const {
    using common::uiText;
    const auto total=std::accumulate(images.begin(),images.end(),uint64_t(0));
    QStringList parts;
    if(total)parts<<uiText("Disk queue discarded %1 images (cam0/1/2/3: %2/%3/%4/%5).",
                          "写盘队列已丢弃 %1 张图像（cam0/1/2/3：%2/%3/%4/%5）。")
        .arg(total).arg(images[0]).arg(images[1]).arg(images[2]).arg(images[3]);
    if(other_sets||partial_sets)parts<<uiText("Other discarded sets: %1; incomplete input sets: %2.",
                                             "其他丢弃帧集：%1；输入缺路帧集：%2。")
        .arg(other_sets).arg(partial_sets);
    return parts.empty()?QString{}:uiText("WARNING: recording has missing images. ","警告：本次录制存在图像缺失。 ")+parts.join(' ');
  }
};
inline QString recordingLossStyle(){return QStringLiteral(
    "background:#fffaeb;color:#b54708;border:1px solid #fedf89;"
    "border-radius:5px;padding:4px 8px;font-weight:600;");}
}
