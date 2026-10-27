#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace prism_viewer::ui {
// Display diagnostic only. No resampling, interpolation, dropped input, or
// changes to recording. A fixed-size window measures actual device intervals.
class ImuSampleRate {
 public:
  void add(uint64_t timestamp, uint16_t sequence, bool synced,
           bool reanchor, bool gap) {
    ++sample_count_;
    const bool continuous=!initialized_ || uint16_t(sequence-last_sequence_)==1;
    if (gap || !continuous) ++sequence_faults_;
    if (!timestamp) ++clock_faults_;
    const bool same_domain=initialized_ && synced==last_synced_;
    if (!timestamp || gap || !continuous || !same_domain) {
      clearWindow();
      anchor_count_=0;
    } else if (!reanchor) {
      if (timestamp<=last_timestamp_) {
        ++clock_faults_;
        clearWindow();
        anchor_count_=0;
      } else {
        const auto delta=timestamp-last_timestamp_;
        if (count_==deltas_.size()) sum_-=deltas_[position_];
        else ++count_;
        deltas_[position_]=delta;
        position_=(position_+1)%deltas_.size();
        sum_+=delta;
        ++intervals_; sum_all_+=delta;
        min_=min_?std::min(min_,delta):delta; max_=std::max(max_,delta);
      }
    }
    // Prefer sample counts between validated PPS-tagged samples. This includes
    // the actual oscillator cadence, rather than mistaking an extrapolated
    // nominal per-sample interval for a physical sampling-rate measurement.
    if (timestamp && synced && reanchor && continuous && !gap && same_domain) {
      if (anchor_count_ &&
          (timestamp<=anchors_[anchor_count_-1].timestamp ||
           timestamp-anchors_[anchor_count_-1].timestamp>2000000)) {
        ++clock_faults_;anchor_count_=0;
      }
      if (anchor_count_==anchors_.size()) {
        for (std::size_t i=1;i<anchors_.size();++i) anchors_[i-1]=anchors_[i];
        --anchor_count_;
      }
      anchors_[anchor_count_++]={timestamp,sample_count_};
    }
    initialized_=timestamp!=0;
    last_timestamp_=timestamp;last_sequence_=sequence;last_synced_=synced;
  }
  double rate() const {
    if (anchored()) {
      const auto& first=anchors_[0];const auto& last=anchors_[anchor_count_-1];
      return 1e6*double(last.sample-first.sample)/double(last.timestamp-first.timestamp);
    }
    return count_>=8 && sum_ ? 1e6*double(count_)/double(sum_) : 0;
  }
  bool anchored() const { return anchor_count_>=2; }
  uint64_t sequenceFaults() const { return sequence_faults_; }
  uint64_t clockFaults() const { return clock_faults_; }
  uint64_t intervals() const { return intervals_; }
  uint64_t sumUs() const { return sum_all_; }
  uint64_t minUs() const { return min_; }
  uint64_t maxUs() const { return max_; }
 private:
  void clearWindow() { count_=position_=0;sum_=0; }
  std::array<uint64_t,1600> deltas_{};
  struct Anchor { uint64_t timestamp=0,sample=0; };
  std::array<Anchor,6> anchors_{};
  std::size_t anchor_count_=0;
  uint64_t sample_count_=0;
  std::size_t count_=0,position_=0;
  uint64_t sum_=0,last_timestamp_=0,intervals_=0,sum_all_=0,min_=0,max_=0;
  uint64_t sequence_faults_=0,clock_faults_=0;
  uint16_t last_sequence_=0;
  bool initialized_=false,last_synced_=false;
};
}
