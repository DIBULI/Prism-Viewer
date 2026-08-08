#include "ui/camera_encoding_panel.hpp"

#include "common/ui_text.hpp"

#include <QtCore/QSignalBlocker>
#include <QtCore/Qt>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSlider>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QVBoxLayout>

namespace prism_viewer::ui {
namespace {

using prism_viewer::common::uiText;

QString qualityHint(int quality) {
  if (quality >= 90) {
    return uiText(
        "Very high detail · maximum USB bandwidth and larger datasets",
        "极高细节 · USB 带宽占用和数据集体积最大");
  }
  if (quality >= 80) {
    return uiText("High detail · increased bandwidth",
                  "高细节 · 带宽占用增加");
  }
  if (quality >= 60) {
    return uiText("Balanced detail and bandwidth (70 is the default)",
                  "细节与带宽均衡（默认值为 70）");
  }
  return uiText("Smaller frames · softer fine detail",
                "帧更小 · 精细纹理更柔和");
}

}  // namespace

CameraEncodingPanel::CameraEncodingPanel(QWidget* parent) : QWidget(parent) {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(8);

  auto* group =
      new QGroupBox(uiText("MJPEG Encoding", "MJPEG 编码"), this);
  auto* group_layout = new QVBoxLayout(group);
  group_layout->setSpacing(10);

  message_label_ = new QLabel(
      uiText("Open a device to read the persistent encoder setting",
             "请先打开设备以读取持久化编码设置"),
      group);
  message_label_->setWordWrap(true);
  group_layout->addWidget(message_label_);

  auto* quality_header = new QHBoxLayout();
  auto* quality_label =
      new QLabel(uiText("JPEG quality", "JPEG 质量"), group);
  quality_spin_ = new QSpinBox(group);
  quality_spin_->setRange(static_cast<int>(prism::kMjpegQualityMin),
                          static_cast<int>(prism::kMjpegQualityMax));
  quality_spin_->setValue(static_cast<int>(prism::kMjpegQualityDefault));
  quality_spin_->setSuffix(QStringLiteral(" / 99"));
  quality_spin_->setMinimumWidth(92);
  quality_header->addWidget(quality_label);
  quality_header->addStretch(1);
  quality_header->addWidget(quality_spin_);
  group_layout->addLayout(quality_header);

  quality_slider_ = new QSlider(Qt::Horizontal, group);
  quality_slider_->setRange(static_cast<int>(prism::kMjpegQualityMin),
                            static_cast<int>(prism::kMjpegQualityMax));
  quality_slider_->setValue(static_cast<int>(prism::kMjpegQualityDefault));
  quality_slider_->setTickPosition(QSlider::TicksBelow);
  quality_slider_->setTickInterval(10);
  group_layout->addWidget(quality_slider_);

  quality_hint_label_ = new QLabel(group);
  quality_hint_label_->setWordWrap(true);
  quality_hint_label_->setStyleSheet(QStringLiteral(
      "color: #475467; background: #f8fafc; border: 1px solid #e4e7ec;"
      "border-radius: 6px; padding: 7px 9px;"));
  group_layout->addWidget(quality_hint_label_);

  auto* explanation = new QLabel(
      uiText("The setting is stored on the device. It is applied when the "
             "camera pipeline starts next time, so capture must be stopped.",
             "该设置保存在设备上，并在下一次启动相机管线时生效，因此修改时"
             "必须停止采集。"),
      group);
  explanation->setWordWrap(true);
  explanation->setStyleSheet(QStringLiteral("color: #667085;"));
  group_layout->addWidget(explanation);

  auto* actions = new QHBoxLayout();
  refresh_button_ =
      new QPushButton(uiText("Refresh", "刷新"), group);
  apply_button_ =
      new QPushButton(uiText("Save Quality", "保存质量"), group);
  refresh_button_->setMinimumWidth(96);
  apply_button_->setMinimumWidth(118);
  actions->addWidget(refresh_button_);
  actions->addWidget(apply_button_);
  actions->addStretch(1);
  group_layout->addLayout(actions);

  root->addWidget(group);
  root->addStretch(1);

  connect(refresh_button_, &QPushButton::clicked, this, [this]() {
    if (on_refresh) on_refresh();
  });
  connect(apply_button_, &QPushButton::clicked, this, [this]() {
    if (on_apply) on_apply(static_cast<uint32_t>(quality_spin_->value()));
  });
  connect(quality_slider_, &QSlider::valueChanged, this, [this](int value) {
    const QSignalBlocker blocker(quality_spin_);
    quality_spin_->setValue(value);
    refreshView();
  });
  connect(quality_spin_,
          QOverload<int>::of(&QSpinBox::valueChanged),
          this, [this](int value) {
            const QSignalBlocker blocker(quality_slider_);
            quality_slider_->setValue(value);
            refreshView();
          });

  clear();
}

void CameraEncodingPanel::clear() {
  configuration_ = {};
  has_configuration_ = false;
  controls_locked_ = false;
  capture_active_ = false;
  busy_ = false;
  busy_message_.clear();
  operation_error_.clear();
  {
    const QSignalBlocker slider_blocker(quality_slider_);
    const QSignalBlocker spin_blocker(quality_spin_);
    quality_slider_->setValue(static_cast<int>(prism::kMjpegQualityDefault));
    quality_spin_->setValue(static_cast<int>(prism::kMjpegQualityDefault));
  }
  refreshView();
}

void CameraEncodingPanel::setDeviceOpen(bool open) {
  device_open_ = open;
  if (!open) clear();
  refreshView();
}

void CameraEncodingPanel::setControlsLocked(bool locked) {
  controls_locked_ = locked;
  refreshView();
}

void CameraEncodingPanel::setCaptureActive(bool active) {
  capture_active_ = active;
  refreshView();
}

void CameraEncodingPanel::setBusy(bool busy, const QString& message) {
  busy_ = busy;
  busy_message_ = message;
  if (busy) operation_error_.clear();
  refreshView();
}

void CameraEncodingPanel::setConfiguration(
    const prism::DeviceConfiguration& configuration) {
  configuration_ = configuration;
  has_configuration_ = true;
  operation_error_.clear();
  {
    const QSignalBlocker slider_blocker(quality_slider_);
    const QSignalBlocker spin_blocker(quality_spin_);
    quality_slider_->setValue(static_cast<int>(configuration.mjpeg_quality));
    quality_spin_->setValue(static_cast<int>(configuration.mjpeg_quality));
  }
  refreshView();
}

void CameraEncodingPanel::setError(const QString& error) {
  operation_error_ = error;
  refreshView();
}

bool CameraEncodingPanel::isDirty() const {
  return has_configuration_ &&
         static_cast<uint32_t>(quality_spin_->value()) !=
             configuration_.mjpeg_quality;
}

void CameraEncodingPanel::setMessage(
    const QString& text, bool warning, bool error) {
  message_label_->setText(text);
  if (error) {
    message_label_->setStyleSheet(QStringLiteral(
        "background: #fef3f2; color: #b42318; border: 1px solid #fecdca;"
        "border-radius: 6px; padding: 7px 9px; font-weight: 600;"));
  } else if (warning) {
    message_label_->setStyleSheet(QStringLiteral(
        "background: #fffaeb; color: #b54708; border: 1px solid #fedf89;"
        "border-radius: 6px; padding: 7px 9px; font-weight: 600;"));
  } else {
    message_label_->setStyleSheet(QStringLiteral(
        "background: #ecfdf3; color: #027a48; border: 1px solid #abefc6;"
        "border-radius: 6px; padding: 7px 9px; font-weight: 600;"));
  }
}

void CameraEncodingPanel::refreshView() {
  quality_hint_label_->setText(qualityHint(quality_spin_->value()));
  if (!device_open_) {
    setMessage(uiText("Open a device to configure MJPEG quality",
                      "请先打开设备，再配置 MJPEG 质量"),
               true, false);
  } else if (busy_) {
    setMessage(busy_message_.isEmpty()
                   ? uiText("MJPEG configuration in progress...",
                            "正在配置 MJPEG……")
                   : busy_message_,
               false, false);
  } else if (!operation_error_.isEmpty()) {
    setMessage(uiText("MJPEG configuration failed: %1",
                      "MJPEG 配置失败：%1")
                   .arg(operation_error_),
               false, true);
  } else if (controls_locked_) {
    setMessage(uiText("Wait for the current device operation to finish",
                      "请等待当前设备操作完成"),
               true, false);
  } else if (!has_configuration_) {
    setMessage(uiText("MJPEG quality has not been read",
                      "尚未读取 MJPEG 质量"),
               true, false);
  } else if (capture_active_) {
    setMessage(uiText("Stop capture before changing MJPEG quality",
                      "请停止采集后再修改 MJPEG 质量"),
               true, false);
  } else {
    setMessage(uiText("Persistent MJPEG quality loaded",
                      "已读取持久化 MJPEG 质量"),
               false, false);
  }

  const bool can_edit = device_open_ && has_configuration_ && !busy_ &&
                        !controls_locked_ && !capture_active_;
  quality_slider_->setEnabled(can_edit);
  quality_spin_->setEnabled(can_edit);
  refresh_button_->setEnabled(device_open_ && !busy_ && !controls_locked_ &&
                              !capture_active_);
  apply_button_->setEnabled(can_edit && isDirty());
}

}  // namespace prism_viewer::ui
