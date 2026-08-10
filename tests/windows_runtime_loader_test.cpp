#include "communication/prism_runtime.hpp"

#include <exception>
#include <iostream>

int main() {
  try {
    prism_runtime::Client client;
    if (client.isOpen()) {
      std::cerr << "new runtime-loaded client must be closed\n";
      return 1;
    }
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
