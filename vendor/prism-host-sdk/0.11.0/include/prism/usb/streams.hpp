#pragma once

#include <cstdint>
#include <functional>

#include "prism/usb/common.hpp"
#include "prism/usb/telemetry.hpp"

namespace prism {

class Client;

using ImuSampleHandler = std::function<void(const ImuSample&)>;
using LidarPointBatchHandler =
    std::function<void(const LidarPointBatch&)>;
using LidarImuSampleHandler = std::function<void(const LidarImuSample&)>;

// High-level IMU interface for applications that share the USB receive stream
// with video. Feed every received frame to handleFrame(); IMU protocol parsing
// stays inside the SDK and structured samples are delivered to the handler.
class ImuStream {
 public:
  ImuStream(Client& client, ImuSampleHandler handler);
  ~ImuStream();

  ImuStream(const ImuStream&) = delete;
  ImuStream& operator=(const ImuStream&) = delete;

  void start(uint32_t sensor_count = 2, uint32_t nominal_rate_hz = 0);
  void stop();
  bool active() const noexcept;
  bool handleFrame(const Frame& frame);

 private:
  Client* client_ = nullptr;
  ImuSampleHandler handler_;
  bool active_ = false;
};

class LidarStream {
 public:
  LidarStream(Client& client, LidarPointBatchHandler handler);
  LidarStream(Client& client, LidarPointBatchHandler point_handler,
              LidarImuSampleHandler imu_handler);
  ~LidarStream();

  LidarStream(const LidarStream&) = delete;
  LidarStream& operator=(const LidarStream&) = delete;

  void start(LidarModel model);
  void stop();
  bool active() const noexcept;
  bool handleFrame(const Frame& frame);

 private:
  Client* client_ = nullptr;
  LidarPointBatchHandler point_handler_;
  LidarImuSampleHandler imu_handler_;
  bool active_ = false;
};

}  // namespace prism
