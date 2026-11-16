#include "ui/lidar_rotation_controls.hpp"
#include "common/ui_text.hpp"
#include <QtCore/QSettings>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>

namespace prism_viewer::ui {
using common::uiText;

LidarRotationControls::LidarRotationControls(
    QSettings& settings, LidarPointCloudWidget& live,
    LidarPointCloudWidget& playback, QPushButton& live_button,
    QPushButton& playback_button, QObject* parent)
    : QObject(parent), settings_(settings), live_(live), playback_(playback),
      live_button_(live_button), playback_button_(playback_button) {
  const auto values = settings_.value(QLatin1String(kSettingsKey)).toList();
  if (values.size() == 3) {
    bool ok[3]{};
    Rotation saved{values[0].toDouble(&ok[0]), values[1].toDouble(&ok[1]),
                   values[2].toDouble(&ok[2])};
    if (ok[0] && ok[1] && ok[2] && LidarPointCloudWidget::validDisplayRotation(saved))
      rotation_ = saved;
  }
  updateViews();
  connect(&live_button_, &QPushButton::clicked, this, [this]{edit(&live_button_);});
  connect(&playback_button_, &QPushButton::clicked, this, [this]{edit(&playback_button_);});
}

void LidarRotationControls::updateViews() {
  live_.setDisplayRotation(rotation_);
  playback_.setDisplayRotation(rotation_);
  const auto tooltip = uiText(
      "Roll %1° · Pitch %2° · Yaw %3°\nShared by live and dataset views; recording and export keep original coordinates.",
      "Roll %1° · Pitch %2° · Yaw %3°\n实时与数据集回放共用；录制和导出保留原始坐标。")
      .arg(rotation_.roll_degrees,0,'f',2).arg(rotation_.pitch_degrees,0,'f',2)
      .arg(rotation_.yaw_degrees,0,'f',2);
  for (auto* button : {&live_button_, &playback_button_}) {
    button->setText(uiText("Point-cloud rotation…", "点云旋转…"));
    button->setToolTip(tooltip);
  }
}

void LidarRotationControls::edit(QWidget* parent) {
  QDialog dialog(parent->window());
  dialog.setObjectName(QStringLiteral("lidarRotationDialog"));
  dialog.setWindowTitle(uiText("Point-cloud display rotation", "点云显示旋转"));
  dialog.setMinimumWidth(440);
  auto* layout = new QVBoxLayout(&dialog);
  auto* note = new QLabel(uiText(
      "Shared by live and dataset views. Saved on this computer.\n"
      "Display only; recording and export keep original coordinates.",
      "实时与数据集回放共用，保存在本机。\n仅改变显示，录制和导出保留原始坐标。"), &dialog);
  note->setWordWrap(true);
  layout->addWidget(note);
  auto* form = new QFormLayout();
  std::array<QDoubleSpinBox*,3> angles{};
  const char* names[] = {"lidarRollDegrees", "lidarPitchDegrees", "lidarYawDegrees"};
  const char* labels[] = {"Roll (X)", "Pitch (Y)", "Yaw (Z)"};
  const double values[] = {rotation_.roll_degrees, rotation_.pitch_degrees, rotation_.yaw_degrees};
  for (size_t i=0; i<angles.size(); ++i) {
    auto* spin = new QDoubleSpinBox(&dialog);
    spin->setObjectName(QLatin1String(names[i]));
    spin->setRange(-360.0, 360.0);
    spin->setDecimals(2);
    spin->setSingleStep(1.0);
    spin->setSuffix(QStringLiteral("°"));
    spin->setValue(values[i]);
    angles[i] = spin;
    form->addRow(QLatin1String(labels[i]), spin);
  }
  layout->addLayout(form);
  auto* order = new QLabel(uiText(
      "Right-hand rule; fixed axes X → Y → Z (Roll → Pitch → Yaw).",
      "右手规则；按固定轴 X → Y → Z（Roll → Pitch → Yaw）依次旋转。"), &dialog);
  order->setWordWrap(true);
  layout->addWidget(order);
  auto* buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Reset, &dialog);
  buttons->button(QDialogButtonBox::Ok)->setText(uiText("Apply and remember", "应用并记住"));
  buttons->button(QDialogButtonBox::Cancel)->setText(uiText("Cancel", "取消"));
  buttons->button(QDialogButtonBox::Reset)->setText(uiText("Zero angles", "归零"));
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  connect(buttons->button(QDialogButtonBox::Reset), &QPushButton::clicked, &dialog,
          [angles]{for (auto* spin : angles) spin->setValue(0.0);});
  layout->addWidget(buttons);
  if (dialog.exec() != QDialog::Accepted) return;
  rotation_ = {angles[0]->value(), angles[1]->value(), angles[2]->value()};
  updateViews();
  settings_.setValue(QLatin1String(kSettingsKey),
                    QVariantList{rotation_.roll_degrees, rotation_.pitch_degrees, rotation_.yaw_degrees});
  settings_.sync();
  if (settings_.status() != QSettings::NoError)
    QMessageBox::warning(parent->window(), uiText("Settings not saved", "设置未保存"),
        uiText("Rotation applied, but it could not be saved on this computer.",
               "旋转已应用，但无法在本机保存设置。"));
}
}
