#pragma once

#include "prism/usb/common.hpp"

#include <stdexcept>
#include <string>

namespace prism_viewer {

enum class CaptureProduct { Legacy413GSV0, Strict415RSV0, Mixed21315V0 };

inline bool supportsCapture(prism::DeviceModel model) {
  return model == prism::DeviceModel::Prism413GSV0 ||
         model == prism::DeviceModel::Prism415RSV0 ||
         model == prism::DeviceModel::Prism21315V0;
}

inline CaptureProduct captureProduct(prism::DeviceModel model) {
  switch (model) {
    case prism::DeviceModel::Prism413GSV0: return CaptureProduct::Legacy413GSV0;
    case prism::DeviceModel::Prism415RSV0: return CaptureProduct::Strict415RSV0;
    case prism::DeviceModel::Prism21315V0: return CaptureProduct::Mixed21315V0;
    default: throw std::invalid_argument("DeviceInfo must report a supported device model before capture");
  }
}

inline bool usesProfileCapture(prism::DeviceModel model) {
  return model == prism::DeviceModel::Prism415RSV0 || model == prism::DeviceModel::Prism21315V0;
}

inline unsigned captureOptions(CaptureProduct product) {
  switch (product) {
    case CaptureProduct::Legacy413GSV0: return 0;
    case CaptureProduct::Strict415RSV0: return 7;
    case CaptureProduct::Mixed21315V0: return 3;
  }
  throw std::invalid_argument("unsupported capture product");
}

template <typename Client>
auto startProductCapture(Client& client, CaptureProduct product) {
  switch (product) {
    case CaptureProduct::Legacy413GSV0: return client.startVideo1280x1024();
    case CaptureProduct::Strict415RSV0: return client.startPrism415rsv0Video(2);
    case CaptureProduct::Mixed21315V0: return client.startProfileVideoGpu();
  }
  throw std::invalid_argument("unsupported capture product");
}

}  // namespace prism_viewer
