#pragma once
#include "prism/usb/profile_video.hpp"
#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace prism_viewer::dataset {
struct Raw12RecordingStatus {
  std::filesystem::path root;
  std::array<uint64_t,4> frames{}, received{}, dropped{};
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
  // Tests inject disk latency/failure; production leaves this empty.
  std::function<void()> before_write;
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
  void finish(const std::string& error={}); // Nonblocking; UI polls finalization.
  Raw12RecordingStatus status() const;
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
