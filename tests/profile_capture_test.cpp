#include "communication/profile_capture.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
struct Client {
  unsigned calls = 0;
  unsigned options = 99;
  unsigned fps = 0;
  bool reject = false;
  int start(unsigned value) {
    ++calls;
    options = value;
    if (reject) throw std::runtime_error("Agent rejected product");
    return int(value);
  }
  int startVideo1280x1024() { return start(0); }
  int startPrism415mVideo(unsigned value) { fps=value; return start(7); }
  int startProfileVideoGpu() { return start(3); }
};
}

int main() {
  try {
    for (auto model : {prism::DeviceModel::Prism41M3, prism::DeviceModel::Prism415M, prism::DeviceModel::Prism1315}) {
      const auto product = prism_viewer::captureProduct(model);
      const unsigned expected = model == prism::DeviceModel::Prism41M3 ? 0 : model == prism::DeviceModel::Prism1315 ? 3 : 7;
      require(prism_viewer::captureOptions(product) == expected, "wrong options");
      Client client;
      require(prism_viewer::startProductCapture(client, product) == int(expected),
              "wrong SDK entry");
      require(client.calls == 1 && client.options == expected, "multiple STARTs");
      require(client.fps == (product == prism_viewer::CaptureProduct::Strict415M ? 2u : 0u),
              "415M must request2FPS; other products preserve saved rates");
      client.calls = 0;
      client.reject = true;
      bool threw = false;
      try { prism_viewer::startProductCapture(client, product); }
      catch (const std::runtime_error&) { threw = true; }
      require(threw && client.calls == 1 && client.options == expected,
              "Agent failure was swallowed or retried with another product");
    }
    for (auto model : {prism::DeviceModel::Unknown, static_cast<prism::DeviceModel>(99)}) {
      Client client;
      bool threw = false;
      try {
        prism_viewer::startProductCapture(
            client, prism_viewer::captureProduct(model));
      } catch (const std::invalid_argument&) { threw = true; }
      require(threw && client.calls == 0, "invalid selection sent START");
    }
    std::cout << "41M3=0, 415M=7, 1315=3; invalid/rejected START never retried\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
