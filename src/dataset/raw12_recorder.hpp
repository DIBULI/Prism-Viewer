#pragma once
#include "prism/usb/profile_video.hpp"
#include "prism/usb/telemetry.hpp"
#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace prism_viewer::dataset {
struct Raw12RecordingStatus {
  std::filesystem::path root;
  std::array<uint64_t,4> frames{}, received{}, dropped{};
  std::array<uint64_t,2> imu_received{}, imu_samples{};
  uint64_t imu_queue_peak=0;
  uint64_t bytes=0, queued_bytes=0;
  bool closing=false, done=false, complete=false;
  std::string error;
};
struct Raw12RecordingOptions {
  unsigned fps=1; // Actual qualified input rate, never the product ceiling.
  size_t queue_frames=12;
  uint64_t queue_bytes=192ull*1024*1024;
  uint64_t reserve_bytes=1024ull*1024*1024;
  uint64_t limit_bytes=6ull*1024*1024*1024;
  unsigned required_imu_mask=0; // Viewer supplies the detected onboard IMUs.
  bool record_any_imu=false;
  bool require_all_cameras=true;
  bool companion_streams=false; // streams.info must also be complete for the joint dataset.
  size_t imu_queue_samples=8192; // Separate bounded queue; never evicts samples.
  std::string imu_timestamp_domain="source-preserved";
  // Tests inject disk latency/failure; production leaves this empty.
  std::function<void()> before_write;
  std::function<void()> before_verify;
};
// Writes original GPU-compressed frames, independent of the replaceable preview
// queue. Overflow evicts the oldest queued WHOLE frame, never the in-flight
// write. Oversized incoming frames are rejected; all losses are accounted.
class Raw12Recorder {
 public:
  Raw12Recorder(const std::filesystem::path& parent, Raw12RecordingOptions options);
  ~Raw12Recorder();
  Raw12Recorder(const Raw12Recorder&)=delete;
  Raw12Recorder& operator=(const Raw12Recorder&)=delete;
  void append(const prism::capture::ProfileImage& image) noexcept;
  void appendImu(const prism::ImuSample& sample) noexcept;
  void finish(const std::string& error={}); // Nonblocking; UI polls finalization.
  Raw12RecordingStatus status() const;
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
