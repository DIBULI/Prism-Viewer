#pragma once

#include <functional>
#include <thread>

namespace prism_viewer::control {

// Runs the one long-lived device operation allowed by the Viewer at a time.
// The caller owns operation-specific cancellation flags; this class owns the
// worker lifetime and guarantees that a previous task is joined before reuse.
class OperationController {
 public:
  OperationController() = default;
  ~OperationController();

  OperationController(const OperationController&) = delete;
  OperationController& operator=(const OperationController&) = delete;

  void start(std::function<void()> operation);
  void join();
  bool joinable() const;

 private:
  std::thread worker_;
};

}  // namespace prism_viewer::control
