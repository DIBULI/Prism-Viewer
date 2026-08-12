#include "ui/camera_exposure_panel.hpp"

#include "common/ui_text.hpp"

#include <QtCore/QSignalBlocker>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace prism_viewer::ui {
namespace {

using prism_viewer::common::uiText;

constexpr int kAutomaticModeValue =
    static_cast<int>(prism::CameraExposureMode::Automatic);
constexpr int kManualModeValue =
    static_cast<int>(prism::CameraExposureMode::Manual);

}  // namespace

CameraExposurePanel::CameraExposurePanel(QWidget* parent) : QWidget(parent) {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(8);

  auto* group =
      new QGroupBox(uiText("Runtime Exposure and Gain", "运行时曝光与增益"), this);
  auto* group_layout = new QVBoxLayout(group);
  group_layout->setSpacing(8);

  message_label_ = new QLabel(
      uiText("Open a device to read runtime exposure settings",
             "请先打开设备以读取运行时曝光设置"),
      group);
  message_label_->setWordWrap(true);
  group_layout->addWidget(message_label_);

  auto* target_row = new QHBoxLayout();
  auto* target_label = new QLabel(
      uiText("Shared target brightness", "统一目标亮度"), group);
  target_brightness_ = new QSpinBox(group);
  target_brightness_->setRange(
      prism::kAutoExposureMinTargetBrightness,
      prism::kAutoExposureMaxTargetBrightness);
  target_brightness_->setValue(
      prism::kAutoExposureDefaultTargetBrightness);
  target_brightness_->setToolTip(
      uiText("PL uses RAW8 mean brightness to adjust each automatic camera's "
             "exposure time. All cameras share this target; sensor gain is "
             "configured independently below.",
             "PL 根据 RAW8 平均亮度闭环调整各路自动曝光时间；所有相机共用此"
             "目标；传感器增益在下方分别设置。"));
  target_row->addWidget(target_label);
  target_row->addStretch(1);
  target_row->addWidget(target_brightness_);
  group_layout->addLayout(target_row);

  auto* settings = new QGridLayout();
  settings->setHorizontalSpacing(8);
  settings->setVerticalSpacing(7);
  settings->addWidget(
      new QLabel(uiText("Camera", "相机"), group), 0, 0);
  settings->addWidget(
      new QLabel(uiText("Exposure mode", "曝光模式"), group), 0, 1);
  settings->addWidget(
      new QLabel(uiText("Manual exposure", "手动曝光时间"), group), 0, 2);
  settings->addWidget(
      new QLabel(uiText("SC130GS gain", "SC130GS 增益"), group), 0, 3);

  for (int camera = 0; camera < 4; ++camera) {
    settings->addWidget(
        new QLabel(uiText("Camera %1", "相机 %1").arg(camera), group),
        camera + 1, 0);

    camera_mode_[camera] = new QComboBox(group);
    camera_mode_[camera]->addItem(
        uiText("PL automatic exposure", "PL 自动曝光"),
        kAutomaticModeValue);
    camera_mode_[camera]->addItem(
        uiText("Manual", "手动"), kManualModeValue);
    camera_mode_[camera]->setMinimumWidth(150);
    settings->addWidget(camera_mode_[camera], camera + 1, 1);

    manual_exposure_us_[camera] = new QSpinBox(group);
    manual_exposure_us_[camera]->setObjectName(
        QStringLiteral("camera%1ExposureSpin").arg(camera));
    manual_exposure_us_[camera]->setRange(
        static_cast<int>(prism::kCameraMinExposureUs),
        static_cast<int>(prism::cameraMaxExposureUs(camera_fps_)));
    manual_exposure_us_[camera]->setValue(
        static_cast<int>(prism::kCameraDefaultExposureUs));
    manual_exposure_us_[camera]->setSuffix(
        uiText(" us", " 微秒"));
    manual_exposure_us_[camera]->setSingleStep(10);
    manual_exposure_us_[camera]->setMinimumWidth(110);
    settings->addWidget(manual_exposure_us_[camera], camera + 1, 2);

    sensor_gain_[camera] = new QDoubleSpinBox(group);
    sensor_gain_[camera]->setObjectName(
        QStringLiteral("camera%1GainSpin").arg(camera));
    sensor_gain_[camera]->setRange(
        static_cast<double>(prism::kCameraMinGainX1024) / 1024.0,
        static_cast<double>(prism::kCameraMaxGainX1024) / 1024.0);
    sensor_gain_[camera]->setDecimals(5);
    sensor_gain_[camera]->setSingleStep(
        static_cast<double>(prism::kCameraGainStepX1024) / 1024.0);
    sensor_gain_[camera]->setValue(
        static_cast<double>(prism::kCameraDefaultGainX1024) / 1024.0);
    sensor_gain_[camera]->setSuffix(QStringLiteral("×"));
    sensor_gain_[camera]->setMinimumWidth(105);
    sensor_gain_[camera]->setToolTip(
        uiText("SC130GS analog sensor gain; valid steps are 1/32×. Higher "
               "gain brightens the image but also increases noise.",
               "SC130GS 传感器模拟增益，步进为 1/32×。提高增益会提亮画面，"
               "也会增加噪声。"));
    settings->addWidget(sensor_gain_[camera], camera + 1, 3);
  }
  settings->setColumnStretch(1, 1);
  settings->setColumnStretch(2, 1);
  settings->setColumnStretch(3, 1);
  group_layout->addLayout(settings);

  auto* actions = new QHBoxLayout();
  refresh_button_ = new QPushButton(
      uiText("Refresh Exposure", "刷新曝光设置"), group);
  apply_button_ = new QPushButton(
      uiText("Apply Runtime Settings", "应用运行时设置"), group);
  refresh_button_->setObjectName(QStringLiteral("cameraExposureRefreshButton"));
  apply_button_->setObjectName(QStringLiteral("cameraExposureApplyButton"));
  refresh_button_->setMinimumWidth(120);
  apply_button_->setMinimumWidth(150);
  apply_button_->setToolTip(
      uiText("Runtime-only: settings return to defaults after restart.",
             "仅在本次运行中生效，设备重启后恢复默认值。"));
  actions->addWidget(refresh_button_);
  actions->addWidget(apply_button_);
  actions->addStretch(1);
  group_layout->addLayout(actions);

  root->addWidget(group);

  connect(refresh_button_, &QPushButton::clicked, this, [this]() {
    if (on_refresh) on_refresh();
  });
  connect(apply_button_, &QPushButton::clicked, this, [this]() {
    if (on_apply) on_apply(editedConfiguration());
  });
  connect(target_brightness_,
          QOverload<int>::of(&QSpinBox::valueChanged),
          this, [this](int) { refreshView(); });
  for (int camera = 0; camera < 4; ++camera) {
    connect(camera_mode_[camera],
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { refreshView(); });
    connect(manual_exposure_us_[camera],
            QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int) { refreshView(); });
    connect(sensor_gain_[camera],
            QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double) { refreshView(); });
  }

  clear();
}

void CameraExposurePanel::clear() {
  configuration_ = {};
  has_configuration_ = false;
  controls_locked_ = false;
  capture_active_ = false;
  busy_ = false;
  busy_message_.clear();
  operation_error_.clear();
  refreshView();
}

void CameraExposurePanel::setDeviceOpen(bool open) {
  device_open_ = open;
  if (!open) clear();
  refreshView();
}

void CameraExposurePanel::setControlsLocked(bool locked) {
  controls_locked_ = locked;
  refreshView();
}

void CameraExposurePanel::setCaptureActive(bool active) {
  capture_active_ = active;
  refreshView();
}

void CameraExposurePanel::setCameraFps(uint32_t camera_fps) {
  const uint32_t maximum_us = prism::cameraMaxExposureUs(camera_fps);
  if (maximum_us == 0u) return;
  camera_fps_ = camera_fps;
  for (size_t camera = 0; camera < manual_exposure_us_.size(); ++camera) {
    const QSignalBlocker blocker(manual_exposure_us_[camera]);
    manual_exposure_us_[camera]->setMaximum(static_cast<int>(maximum_us));
    if (has_configuration_ &&
        configuration_.manual_exposure_time_us[camera] > maximum_us) {
      configuration_.manual_exposure_time_us[camera] = maximum_us;
    }
    if (has_configuration_) {
      manual_exposure_us_[camera]->setValue(static_cast<int>(
          configuration_.manual_exposure_time_us[camera]));
    }
    manual_exposure_us_[camera]->setToolTip(
        uiText("Maximum at %1 FPS: %2 us (frame period minus 5 ms)",
               "%1 FPS 下最大值：%2 微秒（帧周期减 5 毫秒）")
            .arg(camera_fps_)
            .arg(maximum_us));
  }
  refreshView();
}

void CameraExposurePanel::setBusy(bool busy, const QString& message) {
  busy_ = busy;
  busy_message_ = message;
  if (busy) operation_error_.clear();
  refreshView();
}

void CameraExposurePanel::setConfiguration(
    const prism::ExposureConfiguration& configuration) {
  configuration_ = configuration;
  has_configuration_ = true;
  operation_error_.clear();

  const QSignalBlocker target_blocker(target_brightness_);
  target_brightness_->setValue(configuration.target_brightness);
  for (int camera = 0; camera < 4; ++camera) {
    const QSignalBlocker mode_blocker(camera_mode_[camera]);
    const QSignalBlocker exposure_blocker(manual_exposure_us_[camera]);
    const QSignalBlocker gain_blocker(sensor_gain_[camera]);
    const bool automatic =
        (configuration.automatic_camera_mask & (1u << camera)) != 0u;
    camera_mode_[camera]->setCurrentIndex(
        camera_mode_[camera]->findData(
            automatic ? kAutomaticModeValue : kManualModeValue));
    manual_exposure_us_[camera]->setValue(static_cast<int>(
        configuration.manual_exposure_time_us[static_cast<size_t>(camera)]));
    sensor_gain_[camera]->setValue(
        static_cast<double>(
            configuration.gain_x1024[static_cast<size_t>(camera)]) /
        1024.0);
  }
  refreshView();
}

void CameraExposurePanel::setError(const QString& error) {
  operation_error_ = error;
  refreshView();
}

prism::ExposureConfiguration
CameraExposurePanel::editedConfiguration() const {
  prism::ExposureConfiguration edited = configuration_;
  edited.automatic_camera_mask = 0;
  edited.target_brightness =
      static_cast<uint8_t>(target_brightness_->value());
  for (int camera = 0; camera < 4; ++camera) {
    if (camera_mode_[camera]->currentData().toInt() ==
        kAutomaticModeValue) {
      edited.automatic_camera_mask |=
          static_cast<uint8_t>(1u << camera);
    }
    edited.manual_exposure_time_us[static_cast<size_t>(camera)] =
        static_cast<uint32_t>(manual_exposure_us_[camera]->value());
    edited.gain_x1024[static_cast<size_t>(camera)] =
        static_cast<uint32_t>(
            std::llround(sensor_gain_[camera]->value() * 1024.0));
  }
  return edited;
}

bool CameraExposurePanel::isDirty() const {
  if (!has_configuration_) return false;
  const prism::ExposureConfiguration edited = editedConfiguration();
  return edited.automatic_camera_mask !=
             configuration_.automatic_camera_mask ||
         edited.target_brightness != configuration_.target_brightness ||
         edited.manual_exposure_time_us !=
             configuration_.manual_exposure_time_us ||
         edited.gain_x1024 != configuration_.gain_x1024;
}

void CameraExposurePanel::setMessage(
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

void CameraExposurePanel::refreshView() {
  if (!device_open_) {
    setMessage(
        uiText("Open a device to configure camera exposure",
               "请先打开设备，再配置相机曝光"),
        true, false);
  } else if (busy_) {
    setMessage(
        busy_message_.isEmpty()
            ? uiText("Camera exposure operation in progress...",
                     "正在执行相机曝光操作……")
            : busy_message_,
        false, false);
  } else if (!operation_error_.isEmpty()) {
    setMessage(
        uiText("Camera exposure operation failed: %1",
               "相机曝光操作失败：%1")
            .arg(operation_error_),
        false, true);
  } else if (controls_locked_) {
    setMessage(
        uiText("Wait for the current device operation to finish",
               "请等待当前设备操作完成"),
        true, false);
  } else if (!has_configuration_) {
    setMessage(
        uiText("Runtime exposure settings have not been read",
               "尚未读取运行时曝光设置"),
        true, false);
  } else if (capture_active_) {
    setMessage(
        uiText("Live exposure control is available during capture; changes "
               "are not persisted.",
               "采集中可实时调整曝光；设置不会持久化。"),
        false, false);
  } else {
    setMessage(
        uiText("Runtime exposure settings loaded; changes are not persisted.",
               "已读取运行时曝光设置；修改不会持久化。"),
        false, false);
  }

  const bool can_interact =
      device_open_ && has_configuration_ && !busy_ && !controls_locked_;
  target_brightness_->setEnabled(can_interact);
  for (int camera = 0; camera < 4; ++camera) {
    camera_mode_[camera]->setEnabled(can_interact);
    const bool manual =
        camera_mode_[camera]->currentData().toInt() == kManualModeValue;
    manual_exposure_us_[camera]->setEnabled(can_interact && manual);
    sensor_gain_[camera]->setEnabled(can_interact);
  }
  refresh_button_->setEnabled(
      device_open_ && !busy_ && !controls_locked_);
  apply_button_->setEnabled(can_interact && isDirty());
}

}  // namespace prism_viewer::ui
