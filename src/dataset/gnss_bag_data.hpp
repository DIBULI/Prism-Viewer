#pragma once

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QString>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace prism_viewer::dataset::gnss_bag {
using Row = std::map<std::string, std::string>;
inline const std::string& field(const Row& row, const std::string& key) {
  const auto it = row.find(key);
  if (it == row.end()) throw std::runtime_error("GNSS export: missing CSV column " + key);
  return it->second;
}
inline uint64_t integer(const std::string& text) {
  uint64_t value = 0;
  const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
  if (text.empty() || result.ec != std::errc() || result.ptr != text.data() + text.size())
    throw std::runtime_error("GNSS export: invalid unsigned integer");
  return value;
}
inline double number(const std::string& text) {
  if (text.empty()) return std::numeric_limits<double>::quiet_NaN();
  bool ok = false;
  const double value = QString::fromStdString(text).toDouble(&ok);
  if (!ok) throw std::runtime_error("GNSS export: invalid numeric field");
  return value;
}
inline std::vector<std::string> splitCsv(const std::string& line) {
  std::vector<std::string> cells;
  std::string cell;
  bool quoted = false, closed = false;
  for (size_t i = 0; i < line.size(); ++i) {
    const char ch = line[i];
    if (quoted) {
      if (ch != '"') cell += ch;
      else if (i + 1 < line.size() && line[i + 1] == '"') { cell += '"'; ++i; }
      else { quoted = false; closed = true; }
    } else if (ch == ',') { cells.push_back(cell); cell.clear(); closed = false; }
    else if (ch == '"' && cell.empty() && !closed) quoted = true;
    else if (closed || ch == '"') throw std::runtime_error("GNSS export: malformed CSV quoting");
    else cell += ch;
  }
  if (quoted) throw std::runtime_error("GNSS export: unterminated CSV quote");
  cells.push_back(cell);
  return cells;
}
inline void readCsv(const std::filesystem::path& path,
                    const std::function<void(const Row&)>& consume,
                    const std::function<void()>& check) {
  if (!std::filesystem::exists(path)) return;
  std::ifstream input(path);
  if (!input) throw std::runtime_error("GNSS export: cannot open " + path.filename().string());
  std::vector<std::string> columns;
  std::string line;
  size_t count = 0;
  while (std::getline(input, line)) {
    if (line.size() > 65536) throw std::runtime_error("GNSS export: oversized CSV line");
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line.front() == '#') continue;
    if ((++count % 256) == 0) check();
    auto cells = splitCsv(line);
    if (columns.empty()) {
      columns = std::move(cells);
      auto sorted = columns; std::sort(sorted.begin(), sorted.end());
      if (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end())
        throw std::runtime_error("GNSS export: duplicate CSV column");
      continue;
    }
    if (cells.size() != columns.size()) throw std::runtime_error("GNSS export: truncated CSV row in " + path.filename().string());
    Row row;
    for (size_t i = 0; i < cells.size(); ++i) row.emplace(columns[i], std::move(cells[i]));
    consume(row);
  }
  if (!input.eof()) throw std::runtime_error("GNSS export: CSV read failed");
}

struct Position {
  Row original;
  bool rtk = false, valid = false, height_valid = false, covariance_valid = false;
  uint64_t elapsed = 0, stamp = 0, session = 0, sequence = 0, received_ms = 0;
  uint8_t quality = 0;
  uint16_t satellites = 0;
  double latitude = NAN, longitude = NAN, height = NAN;
  double north = NAN, east = NAN, up = NAN;
  std::string source, solution, epoch, time_system;
};
struct Quality { Row original; uint64_t elapsed = 0, stamp = 0; };
struct Data { std::vector<Position> positions; std::vector<Quality> quality; bool gnss = false, rtk = false; };
inline bool sigmaValid(double n, double e, double u) {
  return std::isfinite(n*n) && std::isfinite(e*e) && std::isfinite(u*u) && n > 0 && e > 0 && u > 0;
}
inline uint64_t distance(uint64_t a, uint64_t b) { return a > b ? a - b : b - a; }
inline double utcSeconds(const std::string& text) {
  const double hhmmss = number(text);
  if (!std::isfinite(hhmmss) || hhmmss < 0 || hhmmss >= 240000) return NAN;
  const auto h = int(hhmmss / 10000), m = int(hhmmss / 100) % 100;
  const double s = hhmmss - h * 10000 - m * 100;
  return h < 24 && m < 60 && s < 60 ? h * 3600 + m * 60 + s : NAN;
}

// Bag scheduling uses the nearest synchronized IMU arrival anchor, not the
// host wall clock. This is approximate receive timing, NOT a GNSS measurement
// timestamp. Keep NavSatFix.header.stamp and ReceiverPosition.epoch_us unknown.
// The original receiver epoch/time scale is retained verbatim for downstream use.
inline Data load(const std::filesystem::path& root, const std::function<void()>& check) {
  Data data;
  for (bool rtk : {false, true}) {
    readCsv(root / (rtk ? "gnss_receiver_rtk.csv" : "gnss_receiver.csv"), [&](const Row& row) {
      if (data.positions.size() >= 1000000) throw std::runtime_error("GNSS export: position row limit exceeded");
      Position p; p.original = row; p.rtk = rtk;
      p.elapsed = integer(field(row, "recording_elapsed_us"));
      p.session = integer(field(row, "agent_session"));
      p.sequence = integer(field(row, "sequence"));
      p.received_ms = integer(field(row, "agent_received_monotonic_ms"));
      const auto quality = integer(field(row, "quality")), satellites = integer(field(row, "satellites_used"));
      const auto valid = integer(field(row, "valid"));
      if (quality > 255 || satellites > 65535 || valid > 1) throw std::runtime_error("GNSS export: position integer out of range");
      p.quality = uint8_t(quality); p.satellites = uint16_t(satellites);
      p.source = field(row, "source"); p.solution = field(row, "solution");
      p.epoch = field(row, "receiver_epoch"); p.time_system = field(row, "receiver_time_scale");
      p.latitude = number(field(row, "latitude_deg")); p.longitude = number(field(row, "longitude_deg"));
      p.height = number(field(row, "ellipsoid_height_m"));
      p.north = number(field(row, "north_sigma_m")); p.east = number(field(row, "east_sigma_m")); p.up = number(field(row, "up_sigma_m"));
      p.valid = valid && p.quality != 0 && std::isfinite(p.latitude) && std::isfinite(p.longitude) && std::abs(p.latitude) <= 90 && std::abs(p.longitude) <= 180;
      p.height_valid = p.valid && std::isfinite(p.height);
      p.covariance_valid = p.valid && sigmaValid(p.north, p.east, p.up);
      data.positions.push_back(std::move(p));
      (rtk ? data.rtk : data.gnss) = true;
    }, check);
  }
  readCsv(root / "gnss_quality.csv", [&](const Row& row) {
    if (data.quality.size() >= 1000000) throw std::runtime_error("GNSS export: quality row limit exceeded");
    data.quality.push_back({row, integer(field(row, "recording_elapsed_us")), 0});
  }, check);
  if (data.positions.empty() && data.quality.empty()) return data;

  // Match GST by session, receiver UTC epoch AND reception proximity. Never
  // forward-fill accuracy from a previous fix, restart, or another day.
  std::map<uint64_t, std::vector<const Quality*>> gst;
  for (const auto& q : data.quality) {
    if (field(q.original, "source").find("GST") == std::string::npos) continue;
    gst[integer(field(q.original, "agent_session"))].push_back(&q);
  }
  for (auto& [session, rows] : gst) {
    (void)session;
    std::stable_sort(rows.begin(), rows.end(), [](auto a, auto b) { return a->elapsed < b->elapsed; });
  }
  for (auto& p : data.positions) {
    check();
    if (p.rtk || !p.valid || p.covariance_valid) continue;
    const auto found = gst.find(p.session); if (found == gst.end()) continue;
    const double epoch = utcSeconds(p.epoch);
    uint64_t best = 2000001;
    const auto& rows = found->second;
    auto it = std::lower_bound(rows.begin(), rows.end(), p.elapsed > 2000000 ? p.elapsed - 2000000 : 0,
                              [](auto q, uint64_t elapsed) { return q->elapsed < elapsed; });
    for (; it != rows.end() && (*it)->elapsed <= p.elapsed + 2000000; ++it) {
      const auto& q = **it;
      const auto delta = distance(q.elapsed, p.elapsed);
      if (delta >= best || distance(integer(field(q.original, "agent_received_monotonic_ms")), p.received_ms) > 2000 ||
          !(std::abs(utcSeconds(field(q.original, "receiver_epoch")) - epoch) < 0.0005)) continue;
      const auto n = number(field(q.original, "north_sigma_m")), e = number(field(q.original, "east_sigma_m")), u = number(field(q.original, "up_sigma_m"));
      if (!sigmaValid(n, e, u)) continue;
      p.north = n; p.east = e; p.up = u; p.covariance_valid = true; best = delta;
    }
  }

  struct Anchor { uint64_t elapsed, stamp; };
  std::vector<Anchor> anchors;
  readCsv(root / "imu_metadata.csv", [&](const Row& row) {
    if (field(row, "phase") != "recording") return;
    const bool viewer = row.count("timestamp_synced") != 0;
    if (viewer && integer(field(row, "timestamp_synced")) != 1) return;
    // Web only marks accepted, synchronized samples as phase=recording.
    const uint64_t elapsed = integer(field(row, viewer ? "recording_elapsed_us" : "elapsed_us"));
    const uint64_t stamp = integer(field(row, "timestamp_us"));
    if (!stamp) return;
    if (!anchors.empty() && elapsed < anchors.back().elapsed)
      throw std::runtime_error("GNSS export: IMU receive clock regressed");
    if (anchors.empty() || elapsed - anchors.back().elapsed >= 100000)
      anchors.push_back({elapsed, stamp});
  }, check);
  if (anchors.empty()) throw std::runtime_error("GNSS export needs synchronized imu_metadata.csv arrival anchors; cannot invent bag timestamps");
  const auto timestamp = [&](uint64_t elapsed) {
    auto it = std::lower_bound(anchors.begin(), anchors.end(), elapsed,
                              [](const Anchor& a, uint64_t t) { return a.elapsed < t; });
    if (it == anchors.end() || (it != anchors.begin() && distance((it-1)->elapsed, elapsed) <= distance(it->elapsed, elapsed))) --it;
    const auto delta = distance(it->elapsed, elapsed);
    if (delta > 2000000 || (elapsed < it->elapsed && delta > it->stamp) ||
        (elapsed >= it->elapsed && it->stamp > std::numeric_limits<uint64_t>::max() - delta))
      throw std::runtime_error("GNSS export: no nearby synchronized arrival anchor");
    return elapsed >= it->elapsed ? it->stamp + delta : it->stamp - delta;
  };
  for (auto& p : data.positions) p.stamp = timestamp(p.elapsed);
  for (auto& q : data.quality) q.stamp = timestamp(q.elapsed);
  return data;
}
inline std::string metadata(const Row& row, uint64_t stamp) {
  QJsonObject object;
  for (const auto& [key, value] : row) object[QString::fromStdString(key)] = QString::fromStdString(value);
  object["bag_timestamp_us"] = QString::number(qulonglong(stamp));
  object["bag_time_basis"] = "approximate_receive_time_from_synchronized_imu";
  object["measurement_timestamp_valid"] = false;
  return QJsonDocument(object).toJson(QJsonDocument::Compact).toStdString();
}
} // namespace prism_viewer::dataset::gnss_bag
