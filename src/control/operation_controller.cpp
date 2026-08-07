#include "control/operation_controller.hpp"

#include <utility>

namespace prism_viewer::control {

OperationController::~OperationController() {
  join();
}

void OperationController::start(std::function<void()> operation) {
  join();
  worker_ = std::thread(std::move(operation));
}

void OperationController::join() {
  if (worker_.joinable()) {
    worker_.join();
  }
}

bool OperationController::joinable() const {
  return worker_.joinable();
}

}  // namespace prism_viewer::control
