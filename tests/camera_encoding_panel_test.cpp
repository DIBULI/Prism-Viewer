#include "ui/camera_encoding_panel.hpp"

#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSlider>
#include <QtWidgets/QSpinBox>

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  prism_viewer::ui::CameraEncodingPanel panel;

  auto* fps = panel.findChild<QComboBox*>(QStringLiteral("cameraFpsCombo"));
  auto* quality =
      panel.findChild<QSpinBox*>(QStringLiteral("mjpegQualitySpin"));
  auto* slider =
      panel.findChild<QSlider*>(QStringLiteral("mjpegQualitySlider"));
  auto* apply = panel.findChild<QPushButton*>(
      QStringLiteral("cameraSettingsApplyButton"));
  auto* refresh = panel.findChild<QPushButton*>(
      QStringLiteral("cameraSettingsRefreshButton"));
  require(fps != nullptr && quality != nullptr && slider != nullptr &&
              apply != nullptr && refresh != nullptr,
          "camera stream controls are discoverable");
  require(!fps->isEnabled() && !quality->isEnabled() && !apply->isEnabled(),
          "controls are disabled before a device is opened");

  prism::DeviceConfiguration loaded;
  loaded.camera_fps = 20;
  loaded.mjpeg_quality = 92;
  loaded.generation = 7;
  loaded.persisted = true;
  panel.setDeviceOpen(true);
  panel.setConfiguration(loaded);
  require(fps->currentData().toUInt() == 20u,
          "loaded camera FPS is selected");
  require(quality->value() == 92 && slider->value() == 92,
          "loaded JPEG quality is selected");
  require(!apply->isEnabled(), "unchanged settings cannot be saved");

  prism::DeviceConfiguration requested;
  bool apply_called = false;
  panel.on_apply = [&](const prism::DeviceConfiguration& configuration) {
    apply_called = true;
    requested = configuration;
  };
  fps->setCurrentIndex(fps->findData(10u));
  require(apply->isEnabled(), "changing FPS enables save");
  apply->click();
  require(apply_called, "save invokes the apply callback");
  require(requested.camera_fps == 10u && requested.mjpeg_quality == 92u,
          "apply callback includes FPS and JPEG quality");

  panel.setConfiguration(requested);
  quality->setValue(84);
  require(slider->value() == 84 && apply->isEnabled(),
          "changing JPEG quality keeps the slider synchronized");
  apply_called = false;
  apply->click();
  require(apply_called && requested.camera_fps == 10u &&
              requested.mjpeg_quality == 84u,
          "quality-only changes retain the selected FPS");

  bool refresh_called = false;
  panel.on_refresh = [&]() { refresh_called = true; };
  refresh->click();
  require(refresh_called, "refresh invokes the refresh callback");

  panel.setCaptureActive(true);
  require(!fps->isEnabled() && !quality->isEnabled() && !apply->isEnabled() &&
              !refresh->isEnabled(),
          "camera settings are locked while capture is active");

  panel.setCaptureActive(false);
  panel.setControlsLocked(true);
  require(!fps->isEnabled() && !quality->isEnabled() && !apply->isEnabled(),
          "camera settings are locked during another device operation");

  std::cout << "camera encoding panel tests passed\n";
  return 0;
}
