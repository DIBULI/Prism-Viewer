#include "ui/camera_exposure_panel.hpp"

#include <QtWidgets/QApplication>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QPushButton>
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
  require(gain0 != nullptr && exposure0 != nullptr,
          "camera 0 exposure and gain controls are discoverable");
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
  panel.setDeviceOpen(true);
  panel.setConfiguration(loaded);
  require(gain0->isEnabled() && near(gain0->value(), 1.0),
          "read-back gain is shown and editable");

  prism::ExposureConfiguration requested;
  bool apply_called = false;
  panel.on_apply = [&](const prism::ExposureConfiguration& configuration) {
    apply_called = true;
    requested = configuration;
  };
  gain0->setValue(3.03125);
  QPushButton* apply = panel.findChild<QPushButton*>(
      QStringLiteral("cameraExposureApplyButton"));
  require(apply != nullptr, "changing gain enables the apply button");
  apply->click();
  require(apply_called && requested.gain_x1024[0] == 3104u,
          "apply callback preserves exact x1024 gain");
  require(requested.gain_x1024[1] == 2048u &&
              requested.gain_x1024[2] == 4096u &&
              requested.gain_x1024[3] == 126976u,
          "unchanged cameras retain their gains");

  panel.setControlsLocked(true);
  require(!gain0->isEnabled(), "gain locks during conflicting operations");

  std::cout << "camera exposure panel tests passed\n";
  return 0;
}
