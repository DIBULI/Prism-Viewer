#pragma once
#include "dataset_playback.hpp"
#include "gnss_bag_data.hpp"
#include <optional>

namespace prism_viewer::dataset {
inline void loadGnssPlayback(const std::filesystem::path& root, DatasetPlaybackData& data) {
  using namespace gnss_bag;
  const auto index = root / "gnss_observations.csv", binary = root / "gnss_observations.bin";
  if (!std::filesystem::exists(index)) return;
  if (std::filesystem::is_symlink(index) || std::filesystem::is_symlink(binary))
    throw std::runtime_error("GNSS playback: symlinked observation file");
  std::ifstream input;
  uint64_t size = 0, retained_bytes = 0;
  if (std::filesystem::is_regular_file(binary)) {
    size = std::filesystem::file_size(binary); input.open(binary, std::ios::binary);
    if (!input) throw std::runtime_error("GNSS playback: cannot open gnss_observations.bin");
  }
  readCsv(index, [&](const Row& row) {
    DatasetPlaybackGnssObservation sample;
    sample.elapsed_us = integer(field(row,"recording_elapsed_us"));
    sample.session = integer(field(row,"agent_session"));
    sample.sequence = integer(field(row,"sequence"));
    sample.received_ms = integer(field(row,"agent_received_monotonic_ms"));
    const auto offset = integer(field(row,"byte_offset")), bytes = integer(field(row,"byte_size"));
    if (integer(field(row,"checksum_valid")) > 1 || !bytes || bytes > 65536 ||
        offset > size || bytes > size-offset || data.gnss_observations.size() >= 1000000 ||
        retained_bytes + bytes > 256ull*1024*1024)
      throw std::runtime_error("GNSS playback: invalid gnss_observations.csv range or size limit");
    if (!data.gnss_observations.empty()) {
      const auto& previous = data.gnss_observations.back();
      if (sample.elapsed_us < previous.elapsed_us ||
          (sample.session == previous.session && sample.sequence <= previous.sequence))
        throw std::runtime_error("GNSS playback: regressed observation order");
      sample.gap = sample.session == previous.session &&
          (sample.sequence-previous.sequence != 1 || sample.received_ms < previous.received_ms);
    }
    sample.sentence.resize(size_t(bytes)); input.seekg(std::streamoff(offset));
    input.read(sample.sentence.data(),std::streamsize(bytes));
    if (!input) throw std::runtime_error("GNSS playback: truncated gnss_observations.bin");
    retained_bytes += bytes; data.gnss_observations.push_back(std::move(sample));
  }, []{});
  if (data.gnss_observations.empty()) return;

  // A recorder cache gap can occur without a skipped sequence number. Retain
  // the discontinuity on the next observation rather than joining the track.
  readCsv(root/"gnss_status.csv", [&](const Row& row) {
    if (integer(field(row,"cache_gap")) != 1) return;
    const auto elapsed = integer(field(row,"recording_elapsed_us"));
    const auto session = integer(field(row,"agent_session"));
    auto it = std::lower_bound(data.gnss_observations.begin(),data.gnss_observations.end(),elapsed,
        [](const DatasetPlaybackGnssObservation& sample,uint64_t t){return sample.elapsed_us<t;});
    if (it != data.gnss_observations.end() && it->session == session) it->gap = true;
  }, []{});

  struct Anchor { uint64_t elapsed, stamp; };
  std::vector<Anchor> anchors;
  readCsv(root/"imu_metadata.csv", [&](const Row& row) {
    // RAW profile host_elapsed_us has a different origin; it is not an anchor
    // for DatasetRecorder's recording_elapsed_us. Leave that data independent.
    if (!row.count("phase") || field(row,"phase") != "recording") return;
    const bool viewer = row.count("timestamp_synced") != 0;
    if (viewer && integer(field(row,"timestamp_synced")) != 1) return;
    const auto elapsed = integer(field(row,viewer?"recording_elapsed_us":"elapsed_us"));
    const auto stamp = integer(field(row,"timestamp_us")); if (!stamp) return;
    if (!anchors.empty() && elapsed < anchors.back().elapsed)
      throw std::runtime_error("GNSS playback: IMU arrival clock regressed");
    if (anchors.empty() || elapsed-anchors.back().elapsed >= 100000) anchors.push_back({elapsed,stamp});
  }, []{});
  const auto mapped = [&](uint64_t elapsed) -> std::optional<uint64_t> {
    if (anchors.empty()) return {};
    auto it = std::lower_bound(anchors.begin(),anchors.end(),elapsed,[](const Anchor& a,uint64_t t){return a.elapsed<t;});
    if (it == anchors.end() || (it != anchors.begin() && distance((it-1)->elapsed,elapsed) <= distance(it->elapsed,elapsed))) --it;
    const auto delta = distance(it->elapsed,elapsed);
    if (delta > 2000000 || (elapsed < it->elapsed && delta > it->stamp) ||
        (elapsed >= it->elapsed && it->stamp > UINT64_MAX-delta)) return {};
    return elapsed >= it->elapsed ? it->stamp+delta : it->stamp-delta;
  };
  uint64_t previous_stamp = 0;
  for (auto& sample : data.gnss_observations) {
    const auto stamp = mapped(sample.elapsed_us);
    if (!stamp || *stamp < previous_stamp) { data.gnss_independent = true; break; }
    sample.timestamp_us = previous_stamp = *stamp;
  }
  auto& timeline = data.gnss_independent ? data.gnss_timeline : data.timeline;
  for (size_t i=0;i<data.gnss_observations.size();++i) {
    auto& sample = data.gnss_observations[i];
    if (data.gnss_independent) sample.timestamp_us = sample.elapsed_us;
    timeline.push_back({sample.timestamp_us,DatasetPlaybackEventType::GnssObservation,i});
  }
}
} // namespace prism_viewer::dataset
