#include "ui/camera_exposure_panel.hpp"

#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QSpinBox>

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

bool near(double actual, double expected) {
  return std::abs(actual - expected) < 1e-9;
}

}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  prism_viewer::ui::CameraExposurePanel panel;

  auto* gain0 = panel.findChild<QDoubleSpinBox*>(
      QStringLiteral("camera0GainSpin"));
  auto* exposure0 = panel.findChild<QSpinBox*>(
      QStringLiteral("camera0ExposureSpin"));
  auto* mode0 = panel.findChild<QComboBox*>(
      QStringLiteral("camera0ExposureModeCombo"));
  auto* min_exposure = panel.findChild<QSpinBox*>(
      QStringLiteral("cameraMinExposureSpin"));
  auto* max_exposure = panel.findChild<QSpinBox*>(
      QStringLiteral("cameraMaxExposureSpin"));
  auto* min_gain = panel.findChild<QDoubleSpinBox*>(
      QStringLiteral("cameraMinGainSpin"));
  auto* max_gain = panel.findChild<QDoubleSpinBox*>(
      QStringLiteral("cameraMaxGainSpin"));
  auto* message = panel.findChild<QLabel*>(
      QStringLiteral("cameraExposureMessage"));
  require(gain0 != nullptr && exposure0 != nullptr,
          "camera 0 exposure and gain controls are discoverable");
  require(mode0 != nullptr && min_exposure != nullptr &&
              max_exposure != nullptr && min_gain != nullptr &&
              max_gain != nullptr && message != nullptr,
          "automatic exposure limit controls are discoverable");
  require(message->sizePolicy().verticalPolicy() == QSizePolicy::Maximum,
          "exposure status remains content-height instead of stretching");
  require(!gain0->isEnabled(), "gain is disabled before opening a device");
  require(near(gain0->minimum(), 1.0) && near(gain0->maximum(), 124.0) &&
              near(gain0->singleStep(), 0.03125),
          "gain range and 1/32x step match SC130GS");
  require(near(gain0->value(), 1.0), "default sensor gain is 1x");
  panel.setCameraFps(1u);
  require(exposure0->maximum() == 995000,
          "1 FPS exposes the 995 ms manual ceiling");
  panel.setCameraFps(15u);
  require(exposure0->maximum() == 61666,
          "15 FPS exposes the derived manual ceiling");
  panel.setCameraFps(30u);
  require(exposure0->maximum() == 28333,
          "30 FPS exposes the 28.333 ms manual ceiling");

  prism::ExposureConfiguration loaded;
  loaded.gain_x1024 = {1024u, 2048u, 4096u, 126976u};
  prism::ExposureLimits loaded_limits;
  loaded_limits.min_exposure_time_us = 500u;
  loaded_limits.max_exposure_time_us = 20000u;
  loaded_limits.effective_max_exposure_time_us = 20000u;
  loaded_limits.min_gain_x1024 = 2048u;
  loaded_limits.max_gain_x1024 = 16384u;
  panel.setDeviceOpen(true);
  panel.setConfiguration(loaded, loaded_limits);
  require(!gain0->isEnabled() && near(gain0->value(), 2.0),
          "automatic camera shows read-back manual gain but disables it");
  require(min_exposure->value() == 500 && max_exposure->value() == 20000 &&
              near(min_gain->value(), 2.0) &&
              near(max_gain->value(), 16.0),
          "read-back automatic limits are shown");
  require(exposure0->minimum() == 500 && exposure0->maximum() == 20000 &&
              near(gain0->minimum(), 2.0) && near(gain0->maximum(), 16.0),
          "manual controls are clamped to the shared limits");

  mode0->setCurrentIndex(1);
  require(gain0->isEnabled() && exposure0->isEnabled(),
          "manual camera enables its exposure and gain controls");

  prism::ExposureConfiguration requested;
  prism::ExposureLimits requested_limits;
  bool apply_called = false;
  panel.on_apply = [&](const prism::ExposureConfiguration& configuration,
                       const prism::ExposureLimits& limits) {
    apply_called = true;
    requested = configuration;
    requested_limits = limits;
  };
  min_exposure->setValue(750);
  max_exposure->setValue(18000);
  min_gain->setValue(2.0);
  max_gain->setValue(8.0);
  gain0->setValue(3.03125);
  QPushButton* apply = panel.findChild<QPushButton*>(
      QStringLiteral("cameraExposureApplyButton"));
  require(apply != nullptr, "changing gain enables the apply button");
  apply->click();
  require(apply_called && requested.gain_x1024[0] == 3104u,
          "apply callback preserves exact x1024 gain");
  require(requested_limits.min_exposure_time_us == 750u &&
              requested_limits.max_exposure_time_us == 18000u &&
              requested_limits.effective_max_exposure_time_us == 18000u &&
              requested_limits.min_gain_x1024 == 2048u &&
              requested_limits.max_gain_x1024 == 8192u,
          "apply callback includes exposure and gain limits");
  require(requested.gain_x1024[1] == 2048u &&
              requested.gain_x1024[2] == 4096u &&
              requested.gain_x1024[3] == 8192u,
          "unchanged cameras retain valid gains and clamp to the new maximum");

  panel.setControlsLocked(true);
  require(!gain0->isEnabled(), "gain locks during conflicting operations");

  std::cout << "camera exposure panel tests passed\n";
  return 0;
}
