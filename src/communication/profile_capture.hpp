#pragma once

#include "prism/usb/common.hpp"

#include <stdexcept>
#include <string>

namespace prism_viewer {

enum class CaptureProduct { Legacy41M3, Strict415M, Mixed1315 };

inline CaptureProduct captureProduct(prism::DeviceModel model) {
  switch (model) {
    case prism::DeviceModel::Prism41M3: return CaptureProduct::Legacy41M3;
    case prism::DeviceModel::Prism415M: return CaptureProduct::Strict415M;
    case prism::DeviceModel::Prism1315: return CaptureProduct::Mixed1315;
    default: throw std::invalid_argument("DeviceInfo must report a supported device model before capture");
  }
}

inline bool usesProfileCapture(prism::DeviceModel model) {
  return model == prism::DeviceModel::Prism415M || model == prism::DeviceModel::Prism1315;
}

inline unsigned captureOptions(CaptureProduct product) {
  switch (product) {
    case CaptureProduct::Legacy41M3: return 0;
    case CaptureProduct::Strict415M: return 7;
    case CaptureProduct::Mixed1315: return 3;
  }
  throw std::invalid_argument("unsupported capture product");
}

template <typename Client>
auto startProductCapture(Client& client, CaptureProduct product) {
  switch (product) {
    case CaptureProduct::Legacy41M3: return client.startVideo1280x1024();
    case CaptureProduct::Strict415M: return client.startPrism415mVideo(2);
    case CaptureProduct::Mixed1315: return client.startProfileVideoGpu();
  }
  throw std::invalid_argument("unsupported capture product");
}

}  // namespace prism_viewer
