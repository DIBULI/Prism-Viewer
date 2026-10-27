#pragma once
#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <utility>

namespace prism_viewer::ui {
struct PreviewGroupKey {
  uint32_t group_id = 0;
  uint64_t timestamp = 0;
  bool common_trigger = false;
  bool operator==(const PreviewGroupKey& other) const {
    // group_id is shared, never a RAW camera-local counter.
    return common_trigger == other.common_trigger &&
        group_id == other.group_id && timestamp == other.timestamp;
  }
};

// Display-only barrier, downstream of transport ACK/recording. One current
// group, one pending UI batch, one queued drain. No device-time rewriting.
template <typename Frame>
class GroupedPreviewMailbox {
 public:
  using Batch = std::array<std::optional<Frame>, 4>;
  struct Arrival { uint64_t token = 0; bool post = false; };
  struct Delivery {
    Batch frames;
    uint64_t warnings = 0;
    uint8_t unfinished = 0;
    uint32_t group_id = 0;
  };
  void reset(uint64_t generation) {
    std::lock_guard<std::mutex> lock(mutex_);
    generation_ = generation;
    current_ = {}; ready_ = {}; last_ = {};
    posted_ = presenting_ = warning_dirty_ = false;
    warnings_ = 0; unfinished_ = 0; warning_source_ = 0;
  }
  Arrival arrive(PreviewGroupKey key, uint64_t generation, uint8_t expected,
                 int64_t now_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (generation != generation_ || !(expected & 15)) return {};
    if (last_ && key == *last_)
      return {current_ && !current_->closed ? current_->token : 0, false};
    if (last_) {
      // Both paths use real shared IDs. Timestamps may step during time sync,
      // but a same-ID/different-time frame must never join this group.
      const uint32_t delta = key.group_id - last_->group_id;
      if (key.common_trigger != last_->common_trigger ||
          (!delta || delta >= 0x80000000u))
        return {};
    }
    if (current_ && !current_->closed) retireIncomplete();
    else if (ready_ || presenting_) warn(0, last_ ? last_->group_id : key.group_id);
    last_ = key;
    current_ = Group{++token_, key, uint8_t(expected & 15), 0, now_ms, false, {}};
    return {current_->token, requestPost()};
  }
  bool publish(uint64_t token, uint64_t generation, unsigned camera, Frame frame) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (generation != generation_ || camera >= 4 || !current_ ||
        current_->token != token || current_->closed ||
        !(current_->expected & (1u << camera)) || (current_->decoded & (1u << camera)))
      return false;
    current_->frames[camera] = std::move(frame);
    current_->decoded |= uint8_t(1u << camera);
    if (current_->decoded == current_->expected) {
      ready_ = std::move(current_->frames);
      current_->closed = true;
    }
    return requestPost();
  }
  bool expire(uint64_t generation, int64_t now_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (generation != generation_) return false;
    // Missing next-frame notification must never leave healthy images waiting
    // forever. A normal next arrival releases the preceding group earlier.
    if (current_ && !current_->closed && now_ms - current_->started >= 1000)
      retireIncomplete();
    return requestPost();
  }
  Delivery take(uint64_t generation) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (generation != generation_) return {};
    Delivery result;
    if (ready_) { result.frames = std::move(*ready_); ready_.reset(); }
    presenting_ = true;
    result.warnings = warnings_; result.unfinished = unfinished_;
    result.group_id = warning_source_;
    warning_dirty_ = false;
    return result;
  }
  bool presented(uint64_t generation) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (generation != generation_) return false;
    presenting_ = posted_ = false;
    return requestPost();
  }
 private:
  struct Group {
    uint64_t token;
    PreviewGroupKey key;
    uint8_t expected, decoded;
    int64_t started;
    bool closed;
    Batch frames;
  };
  void warn(uint8_t mask, uint32_t source) {
    ++warnings_; unfinished_ = mask; warning_source_ = source; warning_dirty_ = true;
  }
  void retireIncomplete() {
    warn(uint8_t(current_->expected & ~current_->decoded), current_->key.group_id);
    if (current_->decoded) ready_ = std::move(current_->frames);
    current_->closed = true;
  }
  bool requestPost() {
    if (posted_ || (!ready_ && !warning_dirty_)) return false;
    posted_ = true; return true;
  }
  std::mutex mutex_;
  uint64_t generation_ = 0, token_ = 0, warnings_ = 0;
  std::optional<PreviewGroupKey> last_;
  std::optional<Group> current_;
  std::optional<Batch> ready_;
  uint8_t unfinished_ = 0;
  uint32_t warning_source_ = 0;
  bool posted_ = false, presenting_ = false, warning_dirty_ = false;
};
}  // namespace prism_viewer::ui
