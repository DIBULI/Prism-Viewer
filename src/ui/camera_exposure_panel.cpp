#include "ui/camera_exposure_panel.hpp"

#include "common/ui_text.hpp"

#include <QtCore/QSignalBlocker>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QFrame>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSizePolicy>
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
  message_label_->setObjectName(QStringLiteral("cameraExposureMessage"));
  message_label_->setWordWrap(true);
  message_label_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  message_label_->setSizePolicy(
      QSizePolicy::Preferred, QSizePolicy::Maximum);
  group_layout->addWidget(message_label_);
  exposure_group_mode_ = new QComboBox(group);
  exposure_group_mode_->setObjectName(QStringLiteral("cameraExposureGroupMode"));
  exposure_group_mode_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  exposure_group_mode_->setMinimumContentsLength(12);
  exposure_group_mode_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  exposure_group_mode_->addItem(uiText("Independent exposure", "独立曝光"), false);
  exposure_group_mode_->addItem(uiText("Unified auto · highlight guard",
      "四相机统一自动曝光（过曝保护）"), true);
  exposure_group_mode_->setToolTip(uiText(
      "The brightest camera limits a shared exposure; RAW highlight protection can darken other cameras. Gains remain independent. Requires updated Sensor Board firmware.",
      "以最亮一路约束公共曝光，并检查 RAW 高亮饱和比例；暗的一路可能偏暗。增益仍独立调节。需要新版 Sensor Board 固件。"));
  group_layout->addWidget(exposure_group_mode_);
  connect(exposure_group_mode_, qOverload<int>(&QComboBox::currentIndexChanged),
          this, [this](int index) {
            if (index == 1) for (auto* mode : camera_mode_) {
              const QSignalBlocker blocker(mode);
              mode->setCurrentIndex(mode->findData(kAutomaticModeValue));
            }
            refreshView();
          });

  auto* automatic_limits = new QGridLayout();
  automatic_limits->setHorizontalSpacing(10);
  automatic_limits->setVerticalSpacing(4);

  auto* target_label = new QLabel(
      uiText("Target brightness", "目标亮度"), group);
  target_label->setObjectName(
      QStringLiteral("cameraTargetBrightnessLabel"));
  target_label->setWordWrap(true);
  target_label->setSizePolicy(
      QSizePolicy::Preferred, QSizePolicy::Maximum);
  target_label->setToolTip(
      uiText("Shared target brightness for all cameras using PL automatic "
             "exposure.",
             "所有使用 PL 自动曝光的相机共用此目标亮度。"));
  target_brightness_ = new QSpinBox(group);
  target_brightness_->setObjectName(
      QStringLiteral("cameraTargetBrightnessSpin"));
  target_brightness_->setRange(
      prism::kAutoExposureMinTargetBrightness,
      prism::kAutoExposureMaxTargetBrightness);
  target_brightness_->setValue(
      prism::kAutoExposureDefaultTargetBrightness);
  target_brightness_->setSizePolicy(
      QSizePolicy::Expanding, QSizePolicy::Fixed);
  target_brightness_->setToolTip(
      uiText("PL uses the observed RAW image brightness for automatic "
             "control. It raises exposure first and only raises gain after "
             "the exposure limit is reached; when reducing brightness it "
             "reduces gain first.",
             "PL 根据实际 RAW 画面亮度进行自动控制：增亮时优先增加曝光时间，"
             "达到曝光上限后才提高增益；降低亮度时优先降低增益。"));

  min_exposure_us_ = new QSpinBox(group);
  min_exposure_us_->setObjectName(
      QStringLiteral("cameraMinExposureSpin"));
  min_exposure_us_->setSuffix(uiText(" us", " 微秒"));
  min_exposure_us_->setSingleStep(10);
  min_exposure_us_->setMinimumWidth(105);
  min_exposure_us_->setSizePolicy(
      QSizePolicy::Expanding, QSizePolicy::Fixed);

  max_exposure_us_ = new QSpinBox(group);
  max_exposure_us_->setObjectName(
      QStringLiteral("cameraMaxExposureSpin"));
  max_exposure_us_->setSuffix(uiText(" us", " 微秒"));
  max_exposure_us_->setSingleStep(10);
  max_exposure_us_->setMinimumWidth(105);
  max_exposure_us_->setSizePolicy(
      QSizePolicy::Expanding, QSizePolicy::Fixed);

  min_gain_ = new QDoubleSpinBox(group);
  min_gain_->setObjectName(QStringLiteral("cameraMinGainSpin"));
  min_gain_->setRange(
      static_cast<double>(prism::kCameraMinGainX1024) / 1024.0,
      static_cast<double>(prism::kCameraMaxGainX1024) / 1024.0);
  min_gain_->setDecimals(5);
  min_gain_->setSingleStep(
      static_cast<double>(prism::kCameraGainStepX1024) / 1024.0);
  min_gain_->setSuffix(QStringLiteral("×"));
  min_gain_->setMinimumWidth(105);
  min_gain_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  min_gain_->setToolTip(
      uiText("Lower gain limit for automatic control and manual camera "
             "settings.",
             "自动控制和各路手动设置共用的增益下限。"));

  max_gain_ = new QDoubleSpinBox(group);
  max_gain_->setObjectName(QStringLiteral("cameraMaxGainSpin"));
  max_gain_->setRange(
      static_cast<double>(prism::kCameraMinGainX1024) / 1024.0,
      static_cast<double>(prism::kCameraMaxGainX1024) / 1024.0);
  max_gain_->setDecimals(5);
  max_gain_->setSingleStep(
      static_cast<double>(prism::kCameraGainStepX1024) / 1024.0);
  max_gain_->setSuffix(QStringLiteral("×"));
  max_gain_->setMinimumWidth(105);
  max_gain_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  max_gain_->setToolTip(
      uiText("Upper gain limit for automatic control and manual camera "
             "settings.",
             "自动控制和各路手动设置共用的增益上限。"));

  effective_max_exposure_label_ = new QLabel(group);
  effective_max_exposure_label_->setObjectName(
      QStringLiteral("cameraEffectiveMaxExposureLabel"));
  effective_max_exposure_label_->setWordWrap(true);
  effective_max_exposure_label_->setAlignment(
      Qt::AlignLeft | Qt::AlignVCenter);
  effective_max_exposure_label_->setSizePolicy(
      QSizePolicy::Preferred, QSizePolicy::Maximum);
  effective_max_exposure_label_->setToolTip(
      uiText("The effective maximum is limited to the frame period minus "
             "5 ms.",
             "实际最高曝光时间受限于帧周期减 5 毫秒。"));

  auto make_field_label = [group](const QString& text,
                                  const QString& object_name) {
    auto* label = new QLabel(text, group);
    label->setObjectName(object_name);
    label->setWordWrap(true);
    label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    return label;
  };
  auto* min_exposure_label = make_field_label(
      uiText("Minimum exposure", "最低曝光时间"),
      QStringLiteral("cameraMinExposureLabel"));
  auto* max_exposure_label = make_field_label(
      uiText("Maximum exposure", "最高曝光时间"),
      QStringLiteral("cameraMaxExposureLabel"));
  auto* min_gain_label = make_field_label(
      uiText("Minimum gain", "最低增益"),
      QStringLiteral("cameraMinGainLabel"));
  auto* max_gain_label = make_field_label(
      uiText("Maximum gain", "最高增益"),
      QStringLiteral("cameraMaxGainLabel"));

  automatic_limits->addWidget(target_label, 0, 0, Qt::AlignBottom);
  automatic_limits->addWidget(
      min_exposure_label, 0, 1, Qt::AlignBottom);
  automatic_limits->addWidget(target_brightness_, 1, 0);
  automatic_limits->addWidget(min_exposure_us_, 1, 1);
  automatic_limits->addWidget(
      max_exposure_label, 2, 0, Qt::AlignBottom);
  automatic_limits->addWidget(min_gain_label, 2, 1, Qt::AlignBottom);
  automatic_limits->addWidget(max_exposure_us_, 3, 0);
  automatic_limits->addWidget(min_gain_, 3, 1);
  automatic_limits->addWidget(max_gain_label, 4, 0, Qt::AlignBottom);
  automatic_limits->addWidget(max_gain_, 5, 0);
  automatic_limits->addWidget(
      effective_max_exposure_label_, 4, 1, 2, 1);
  automatic_limits->setColumnStretch(0, 1);
  automatic_limits->setColumnStretch(1, 1);
  group_layout->addLayout(automatic_limits);

  auto* camera_settings_label = new QLabel(
      uiText("Per-camera settings", "各相机设置"), group);
  camera_settings_label->setObjectName(
      QStringLiteral("cameraExposureSectionTitle"));
  camera_settings_label->setStyleSheet(
      QStringLiteral("font-weight: 600; color: #344054;"));
  group_layout->addWidget(camera_settings_label);

  for (int camera = 0; camera < 4; ++camera) {
    auto* camera_card = new QFrame(group);
    camera_card->setObjectName(
        QStringLiteral("camera%1ExposureCard").arg(camera));
    camera_card->setFrameShape(QFrame::StyledPanel);
    auto* camera_layout = new QGridLayout(camera_card);
    camera_layout->setContentsMargins(8, 7, 8, 8);
    camera_layout->setHorizontalSpacing(8);
    camera_layout->setVerticalSpacing(4);

    auto* camera_label = new QLabel(
        uiText("Camera %1", "相机 %1").arg(camera), camera_card);
    camera_label->setObjectName(
        QStringLiteral("camera%1ExposureLabel").arg(camera));
    camera_label->setStyleSheet(QStringLiteral("font-weight: 600;"));
    camera_layout->addWidget(camera_label, 0, 0);

    camera_mode_[camera] = new QComboBox(camera_card);
    camera_mode_[camera]->setObjectName(
        QStringLiteral("camera%1ExposureModeCombo").arg(camera));
    camera_mode_[camera]->addItem(
        uiText("PL automatic", "PL 自动"),
        kAutomaticModeValue);
    camera_mode_[camera]->addItem(
        uiText("Manual", "手动"), kManualModeValue);
    camera_mode_[camera]->setSizePolicy(
        QSizePolicy::Expanding, QSizePolicy::Fixed);
    camera_mode_[camera]->setToolTip(
        uiText("Exposure mode for this camera.", "此相机的曝光模式。"));
    camera_layout->addWidget(camera_mode_[camera], 0, 1);

    auto* manual_exposure_label = new QLabel(
        uiText("Manual exposure", "手动曝光时间"), camera_card);
    manual_exposure_label->setObjectName(
        QStringLiteral("camera%1ManualExposureLabel").arg(camera));
    manual_exposure_label->setWordWrap(true);
    auto* sensor_gain_label = new QLabel(
        uiText("SC130GS gain", "SC130GS 增益"), camera_card);
    sensor_gain_label->setObjectName(
        QStringLiteral("camera%1GainLabel").arg(camera));
    sensor_gain_label->setWordWrap(true);
    camera_layout->addWidget(
        manual_exposure_label, 1, 0, Qt::AlignBottom);
    camera_layout->addWidget(sensor_gain_label, 1, 1, Qt::AlignBottom);

    manual_exposure_us_[camera] = new QSpinBox(camera_card);
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
    manual_exposure_us_[camera]->setMinimumWidth(105);
    manual_exposure_us_[camera]->setSizePolicy(
        QSizePolicy::Expanding, QSizePolicy::Fixed);
    camera_layout->addWidget(manual_exposure_us_[camera], 2, 0);

    sensor_gain_[camera] = new QDoubleSpinBox(camera_card);
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
    sensor_gain_[camera]->setSizePolicy(
        QSizePolicy::Expanding, QSizePolicy::Fixed);
    sensor_gain_[camera]->setToolTip(
        uiText("SC130GS analog sensor gain; valid steps are 1/32×. Higher "
               "gain brightens the image but also increases noise.",
               "SC130GS 传感器模拟增益，步进为 1/32×。提高增益会提亮画面，"
               "也会增加噪声。"));
    camera_layout->addWidget(sensor_gain_[camera], 2, 1);
    camera_layout->setColumnStretch(0, 1);
    camera_layout->setColumnStretch(1, 1);
    group_layout->addWidget(camera_card);
  }

  auto* actions = new QHBoxLayout();
  refresh_button_ = new QPushButton(
      uiText("Refresh", "刷新"), group);
  apply_button_ = new QPushButton(
      uiText("Apply Settings", "应用设置"), group);
  refresh_button_->setObjectName(QStringLiteral("cameraExposureRefreshButton"));
  apply_button_->setObjectName(QStringLiteral("cameraExposureApplyButton"));
  refresh_button_->setMinimumWidth(100);
  apply_button_->setMinimumWidth(100);
  refresh_button_->setSizePolicy(
      QSizePolicy::Expanding, QSizePolicy::Fixed);
  apply_button_->setSizePolicy(
      QSizePolicy::Expanding, QSizePolicy::Fixed);
  refresh_button_->setToolTip(
      uiText("Reload runtime exposure settings from the device.",
             "从设备重新读取运行时曝光设置。"));
  apply_button_->setToolTip(
      uiText("Runtime-only: settings return to defaults after restart.",
             "仅在本次运行中生效，设备重启后恢复默认值。"));
  actions->setSpacing(8);
  actions->addWidget(refresh_button_, 1);
  actions->addWidget(apply_button_, 1);
  group_layout->addStretch(1);
  group_layout->addLayout(actions);

  root->addWidget(group);

  connect(refresh_button_, &QPushButton::clicked, this, [this]() {
    if (on_refresh) on_refresh();
  });
  connect(apply_button_, &QPushButton::clicked, this, [this]() {
    if (on_apply) on_apply(editedConfiguration(), editedLimits());
  });
  connect(target_brightness_,
          QOverload<int>::of(&QSpinBox::valueChanged),
          this, [this](int) { refreshView(); });
  connect(min_exposure_us_, QOverload<int>::of(&QSpinBox::valueChanged),
          this, [this](int) {
            updateLimitRanges();
            refreshView();
          });
  connect(max_exposure_us_, QOverload<int>::of(&QSpinBox::valueChanged),
          this, [this](int) {
            updateLimitRanges();
            refreshView();
          });
  connect(min_gain_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          this, [this](double) {
            updateLimitRanges();
            refreshView();
          });
  connect(max_gain_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          this, [this](double) {
            updateLimitRanges();
            refreshView();
          });
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
  exposure_rows_ = false;
  updateExposureUnits();
  configuration_ = {};
  const QSignalBlocker group_blocker(exposure_group_mode_);
  exposure_group_mode_->setCurrentIndex(0);
  limits_ = {};
  {
    const QSignalBlocker min_blocker(min_exposure_us_);
    const QSignalBlocker max_blocker(max_exposure_us_);
    const QSignalBlocker min_gain_blocker(min_gain_);
    const QSignalBlocker gain_blocker(max_gain_);
    min_exposure_us_->setRange(
        static_cast<int>(prism::kCameraMinExposureUs),
        static_cast<int>(prism::kCameraMaxExposureUs));
    max_exposure_us_->setRange(
        static_cast<int>(prism::kCameraMinExposureUs),
        static_cast<int>(prism::kCameraMaxExposureUs));
    min_exposure_us_->setValue(
        static_cast<int>(limits_.min_exposure_time_us));
    max_exposure_us_->setValue(
        static_cast<int>(limits_.max_exposure_time_us));
    min_gain_->setValue(
        static_cast<double>(limits_.min_gain_x1024) / 1024.0);
    max_gain_->setValue(
        static_cast<double>(limits_.max_gain_x1024) / 1024.0);
  }
  has_configuration_ = false;
  controls_locked_ = false;
  capture_active_ = false;
  busy_ = false;
  busy_message_.clear();
  operation_error_.clear();
  setCameraFps(30u);
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

void CameraExposurePanel::setDeviceModel(prism::DeviceModel model) {
  const bool rows = model == prism::DeviceModel::Prism415M;
  if (rows == exposure_rows_) return;
  exposure_rows_ = rows;
  // Do not reinterpret pending edits or a previous device's readback.
  has_configuration_ = false;
  operation_error_.clear();
  updateExposureUnits();
  const QSignalBlocker min_blocker(min_exposure_us_);
  const QSignalBlocker max_blocker(max_exposure_us_);
  min_exposure_us_->setRange(minimumInput(prism::kCameraMinExposureUs),
                            maximumInput(prism::kCameraMaxExposureUs));
  max_exposure_us_->setRange(minimumInput(prism::kCameraMinExposureUs),
                            maximumInput(prism::kCameraMaxExposureUs));
  min_exposure_us_->setValue(minimumInput(prism::kCameraMinExposureUs));
  max_exposure_us_->setValue(maximumInput(prism::kCameraMaxExposureUs));
  updateLimitRanges();
  refreshView();
}

uint32_t CameraExposurePanel::inputToUs(int value) const {
  // Retain SDK wire units; 415M's qualified low-speed row is 62.5 us.
  // These are integration rows, not the separate RB pre-readout delay.
  return exposure_rows_ ? (static_cast<uint32_t>(value) * 125u + 1u) / 2u
                        : static_cast<uint32_t>(value);
}

int CameraExposurePanel::maximumInput(uint32_t us) const {
  return exposure_rows_ ? static_cast<int>(std::min<uint64_t>(15920u,
                                      static_cast<uint64_t>(us) * 2u / 125u))
                        : static_cast<int>(us);
}

int CameraExposurePanel::minimumInput(uint32_t us) const {
  return exposure_rows_ ? static_cast<int>(std::max<uint64_t>(3u,
                              (static_cast<uint64_t>(us) * 2u + 123u) / 125u))
                        : static_cast<int>(us);
}

void CameraExposurePanel::updateExposureUnits() {
  const QString suffix = exposure_rows_ ? uiText(" rows", " 行")
                                        : uiText(" us", " 微秒");
  for (auto* spin : {min_exposure_us_, max_exposure_us_}) {
    spin->setSuffix(suffix);
    spin->setSingleStep(exposure_rows_ ? 1 : 10);
  }
  findChild<QLabel*>(QStringLiteral("cameraMinExposureLabel"))->setText(
      exposure_rows_ ? uiText("Minimum exposure rows", "最小曝光行数")
                     : uiText("Minimum exposure", "最低曝光时间"));
  findChild<QLabel*>(QStringLiteral("cameraMaxExposureLabel"))->setText(
      exposure_rows_ ? uiText("Maximum exposure rows", "最大曝光行数")
                     : uiText("Maximum exposure", "最高曝光时间"));
  for (int camera = 0; camera < 4; ++camera) {
    manual_exposure_us_[camera]->setSuffix(suffix);
    manual_exposure_us_[camera]->setSingleStep(exposure_rows_ ? 1 : 10);
    findChild<QLabel*>(QStringLiteral("camera%1ManualExposureLabel").arg(camera))->setText(
        exposure_rows_ ? uiText("Manual exposure rows", "手动曝光行数")
                       : uiText("Manual exposure", "手动曝光时间"));
    findChild<QLabel*>(QStringLiteral("camera%1GainLabel").arg(camera))->setText(
        exposure_rows_ ? uiText("SCC85HAI gain", "SCC85HAI 增益")
                       : uiText("SC130GS gain", "SC130GS 增益"));
    sensor_gain_[camera]->setToolTip(exposure_rows_
        ? uiText("Sensor gain; valid values follow the device limits.",
                 "传感器增益，允许范围以设备返回的限制为准。")
        : uiText("SC130GS analog sensor gain; valid steps are 1/32×. Higher "
                 "gain brightens the image but also increases noise.",
                 "SC130GS 传感器模拟增益，步进为 1/32×。提高增益会提亮画面，也会增加噪声。"));
  }
  effective_max_exposure_label_->setToolTip(exposure_rows_
      ? uiText("Integration rows, not RB delay rows. Firmware validates trigger and readout limits.",
               "填写积分曝光行数，不是 RB 等待行数；固件校验触发和读出限制。")
      : uiText("The effective maximum is limited to the frame period minus 5 ms.",
               "实际最高曝光时间受限于帧周期减 5 毫秒。"));
  effective_max_exposure_label_->setMinimumHeight(exposure_rows_
      ? effective_max_exposure_label_->fontMetrics().lineSpacing() * 3 : 0);
}

void CameraExposurePanel::setCaptureActive(bool active) {
  // Do not carry unsent gain/auto/limit edits into the row-only live path.
  if (active && !capture_active_ && exposure_rows_ && has_configuration_)
    setConfiguration(configuration_, limits_);
  capture_active_ = active;
  updateLimitRanges();
  refreshView();
}

void CameraExposurePanel::setCameraFps(uint32_t camera_fps) {
  const uint32_t maximum_us = prism::cameraMaxExposureUs(camera_fps);
  if (maximum_us == 0u) return;
  camera_fps_ = camera_fps;
  updateLimitRanges();
  refreshView();
}

void CameraExposurePanel::setBusy(bool busy, const QString& message) {
  busy_ = busy;
  busy_message_ = message;
  if (busy) operation_error_.clear();
  refreshView();
}

void CameraExposurePanel::setConfiguration(
    const prism::ExposureConfiguration& configuration,
    const prism::ExposureLimits& limits) {
  if (exposure_rows_) {
    const int low = minimumInput(limits.min_exposure_time_us);
    const int high = maximumInput(std::min({limits.max_exposure_time_us,
        limits.effective_max_exposure_time_us, prism::cameraMaxExposureUs(camera_fps_)}));
    bool representable = low <= high;
    for (uint32_t us : configuration.manual_exposure_time_us) {
      const int rows = maximumInput(us);
      representable = representable && rows >= low && rows <= high && inputToUs(rows) == us;
    }
    if (!representable) {
      has_configuration_ = false;
      setError(uiText("Device exposure readback is incompatible with whole-row input; refresh after updating firmware.",
                      "设备曝光返回值不兼容整数行数输入；请更新配套固件后刷新。"));
      return;
    }
  }
  configuration_ = configuration;
  const QSignalBlocker group_blocker(exposure_group_mode_);
  exposure_group_mode_->setCurrentIndex(configuration.unified_automatic ? 1 : 0);
  limits_ = limits;
  has_configuration_ = true;
  operation_error_.clear();

  const QSignalBlocker target_blocker(target_brightness_);
  const QSignalBlocker min_blocker(min_exposure_us_);
  const QSignalBlocker max_blocker(max_exposure_us_);
  const QSignalBlocker min_gain_blocker(min_gain_);
  const QSignalBlocker max_gain_blocker(max_gain_);
  target_brightness_->setValue(configuration.target_brightness);
  max_exposure_us_->setRange(
      minimumInput(prism::kCameraMinExposureUs),
      maximumInput(prism::kCameraMaxExposureUs));
  max_exposure_us_->setValue(
      maximumInput(limits.max_exposure_time_us));
  min_exposure_us_->setRange(
      minimumInput(prism::kCameraMinExposureUs),
      maximumInput(std::min(
          limits.max_exposure_time_us,
          prism::cameraMaxExposureUs(camera_fps_))));
  min_exposure_us_->setValue(
      minimumInput(limits.min_exposure_time_us));
  min_gain_->setValue(
      static_cast<double>(limits.min_gain_x1024) / 1024.0);
  max_gain_->setValue(
      static_cast<double>(limits.max_gain_x1024) / 1024.0);
  updateLimitRanges();
  for (int camera = 0; camera < 4; ++camera) {
    const QSignalBlocker mode_blocker(camera_mode_[camera]);
    const QSignalBlocker exposure_blocker(manual_exposure_us_[camera]);
    const QSignalBlocker gain_blocker(sensor_gain_[camera]);
    const bool automatic =
        (configuration.automatic_camera_mask & (1u << camera)) != 0u;
    camera_mode_[camera]->setCurrentIndex(
        camera_mode_[camera]->findData(
            automatic ? kAutomaticModeValue : kManualModeValue));
    manual_exposure_us_[camera]->setValue(maximumInput(
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
  edited.unified_automatic = exposure_group_mode_->currentData().toBool();
  edited.automatic_camera_mask = 0;
  edited.target_brightness =
      static_cast<uint8_t>(target_brightness_->value());
  for (int camera = 0; camera < 4; ++camera) {
    if (edited.unified_automatic || camera_mode_[camera]->currentData().toInt() ==
        kAutomaticModeValue) {
      edited.automatic_camera_mask |=
          static_cast<uint8_t>(1u << camera);
    }
    edited.manual_exposure_time_us[static_cast<size_t>(camera)] =
        inputToUs(manual_exposure_us_[camera]->value());
    edited.gain_x1024[static_cast<size_t>(camera)] =
        static_cast<uint32_t>(
            std::llround(sensor_gain_[camera]->value() * 1024.0));
  }
  return edited;
}

prism::ExposureLimits CameraExposurePanel::editedLimits() const {
  prism::ExposureLimits edited = limits_;
  edited.min_exposure_time_us =
      inputToUs(min_exposure_us_->value());
  edited.max_exposure_time_us =
      inputToUs(max_exposure_us_->value());
  // Rounding an old bound for display must not itself request a setting change.
  if (exposure_rows_ && min_exposure_us_->value() == minimumInput(limits_.min_exposure_time_us))
    edited.min_exposure_time_us = limits_.min_exposure_time_us;
  if (exposure_rows_ && max_exposure_us_->value() == maximumInput(limits_.max_exposure_time_us))
    edited.max_exposure_time_us = limits_.max_exposure_time_us;
  edited.effective_max_exposure_time_us = std::min(
      edited.max_exposure_time_us, prism::cameraMaxExposureUs(camera_fps_));
  if (exposure_rows_)
    edited.effective_max_exposure_time_us = std::min(
        edited.effective_max_exposure_time_us, limits_.effective_max_exposure_time_us);
  edited.min_gain_x1024 = static_cast<uint32_t>(
      std::llround(min_gain_->value() * 1024.0));
  edited.max_gain_x1024 = static_cast<uint32_t>(
      std::llround(max_gain_->value() * 1024.0));
  return edited;
}

void CameraExposurePanel::updateLimitRanges() {
  uint32_t maximum_us = prism::cameraMaxExposureUs(camera_fps_);
  if (exposure_rows_ && has_configuration_)
    maximum_us = std::min(maximum_us, limits_.effective_max_exposure_time_us);
  const uint32_t fps_max = static_cast<uint32_t>(maximumInput(maximum_us));
  const int hard_min = minimumInput(prism::kCameraMinExposureUs);
  const int selected_max = std::max(
      hard_min, max_exposure_us_->value());
  const int effective_max = static_cast<int>(
      std::min<uint32_t>(static_cast<uint32_t>(selected_max), fps_max));
  const int manual_max = exposure_rows_ && capture_active_ && supportsLiveRows()
                             ? std::min(effective_max, 4096) : effective_max;

  {
    const QSignalBlocker blocker(min_exposure_us_);
    min_exposure_us_->setRange(hard_min, effective_max);
  }
  {
    const QSignalBlocker blocker(max_exposure_us_);
    max_exposure_us_->setRange(
        min_exposure_us_->value(),
        maximumInput(prism::kCameraMaxExposureUs));
  }

  {
    const QSignalBlocker blocker(min_gain_);
    min_gain_->setMaximum(max_gain_->value());
  }
  {
    const QSignalBlocker blocker(max_gain_);
    max_gain_->setMinimum(min_gain_->value());
  }
  const uint32_t min_gain_x1024 = static_cast<uint32_t>(
      std::llround(min_gain_->value() * 1024.0));
  const uint32_t max_gain_x1024 = static_cast<uint32_t>(
      std::llround(max_gain_->value() * 1024.0));
  for (size_t camera = 0; camera < manual_exposure_us_.size(); ++camera) {
    const QSignalBlocker exposure_blocker(manual_exposure_us_[camera]);
    const QSignalBlocker gain_blocker(sensor_gain_[camera]);
    manual_exposure_us_[camera]->setRange(
        min_exposure_us_->value(), manual_max);
    sensor_gain_[camera]->setMaximum(
        static_cast<double>(max_gain_x1024) / 1024.0);
    sensor_gain_[camera]->setMinimum(
        static_cast<double>(min_gain_x1024) / 1024.0);
    manual_exposure_us_[camera]->setToolTip(
        (exposure_rows_
         ? uiText("Active range at %1 FPS: %2-%3 rows (configured maximum %4 rows)",
                  "%1 FPS 下当前范围：%2-%3 行（配置上限 %4 行）")
         : uiText("Active range at %1 FPS: %2-%3 us (configured maximum %4 us)",
                  "%1 FPS 下当前范围：%2-%3 微秒（配置上限 %4 微秒）"))
            .arg(camera_fps_)
            .arg(min_exposure_us_->value())
            .arg(manual_max)
            .arg(max_exposure_us_->value()));
  }
  effective_max_exposure_label_->setText(
      (exposure_rows_
       ? uiText("Effective max at %1 FPS: %2 rows", "%1 FPS 实际上限：%2 行")
       : uiText("Effective max at %1 FPS: %2 us", "%1 FPS 实际上限：%2 微秒"))
          .arg(camera_fps_)
          .arg(manual_max));
}

bool CameraExposurePanel::supportsLiveRows() const {
  if (!has_configuration_ || configuration_.automatic_camera_mask != 0 ||
      configuration_.unified_automatic) return false;
  for (size_t camera = 0; camera < 4; ++camera) {
    const auto us = configuration_.manual_exposure_time_us[camera];
    const int rows = maximumInput(us);
    if (rows < 3 || rows > 4096 || inputToUs(rows) != us ||
        configuration_.gain_x1024[camera] != 1024u) return false;
  }
  return true;
}

bool CameraExposurePanel::isDirty() const {
  if (!has_configuration_) return false;
  const prism::ExposureConfiguration edited = editedConfiguration();
  const prism::ExposureLimits edited_limits = editedLimits();
  return edited_limits.min_exposure_time_us != limits_.min_exposure_time_us ||
         edited_limits.max_exposure_time_us != limits_.max_exposure_time_us ||
         edited_limits.min_gain_x1024 != limits_.min_gain_x1024 ||
         edited_limits.max_gain_x1024 != limits_.max_gain_x1024 ||
         edited.automatic_camera_mask !=
             configuration_.automatic_camera_mask ||
         edited.target_brightness != configuration_.target_brightness ||
         edited.unified_automatic != configuration_.unified_automatic ||
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
  } else if (capture_active_ && exposure_rows_ && !supportsLiveRows()) {
    setMessage(uiText("Live row control requires manual exposure, 1x gain and 3-4096 rows. Current readback is preserved; configure these settings while idle.",
                      "实时行数控制要求手动曝光、1倍增益和3–4096行。当前读回值保持不变；请在空闲时配置这些设置。"), true, false);
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
      device_open_ && has_configuration_ && !busy_ && !controls_locked_ &&
      (!exposure_rows_ || !capture_active_ || supportsLiveRows());
  const bool live_rows = exposure_rows_ && capture_active_;
  target_brightness_->setEnabled(can_interact && !live_rows);
  exposure_group_mode_->setEnabled(can_interact && !live_rows);
  const bool unified = exposure_group_mode_->currentData().toBool();
  min_exposure_us_->setEnabled(can_interact && !live_rows);
  max_exposure_us_->setEnabled(can_interact && !live_rows);
  min_gain_->setEnabled(can_interact && !live_rows);
  max_gain_->setEnabled(can_interact && !live_rows);
  for (int camera = 0; camera < 4; ++camera) {
    camera_mode_[camera]->setEnabled(can_interact && !unified && !live_rows);
    const bool manual =
        !unified && camera_mode_[camera]->currentData().toInt() == kManualModeValue;
    manual_exposure_us_[camera]->setEnabled(can_interact && manual);
    sensor_gain_[camera]->setEnabled(can_interact && manual && !live_rows);
  }
  refresh_button_->setEnabled(
      device_open_ && !busy_ && !controls_locked_ && !live_rows);
  apply_button_->setEnabled(can_interact && isDirty());
}

}  // namespace prism_viewer::ui
