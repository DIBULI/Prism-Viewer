#include "dataset/raw12_dataset.hpp"
#include "common/ui_text.hpp"
#include "ui/bayer_preview.hpp"
#include "prism/usb/profile_video.hpp"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>

namespace prism_viewer::dataset {
namespace {
using Fields = std::map<std::string, std::string>;
void require(bool ok, const std::string& message) {
  if (!ok) throw std::runtime_error(message);
}
void checkCancelled(const DatasetValidationCancelCallback& cancel) {
  if (cancel && cancel()) throw std::runtime_error("Validation cancelled");
}
void regularLocal(const std::filesystem::path& path) {
  require(std::filesystem::is_regular_file(std::filesystem::symlink_status(path)),
          "Missing or non-regular dataset file: " + path.filename().string());
}
QByteArray readBounded(const std::filesystem::path& path, int limit) {
  regularLocal(path);
  QFile file(common::fromFilesystemPath(path));
  require(file.open(QIODevice::ReadOnly), "Cannot open " + path.filename().string());
  auto bytes = file.read(limit + 1);
  require(bytes.size() <= limit && file.error() == QFile::NoError, "Oversized or unreadable metadata");
  return bytes;
}
uint64_t number(const Fields& fields, const std::string& key) {
  const auto it = fields.find(key);
  require(it != fields.end() && !it->second.empty(), "Missing field: " + key);
  uint64_t value = 0;
  for (char c : it->second) {
    require(c >= '0' && c <= '9', "Invalid integer: " + key);
    require(value <= (UINT64_MAX - unsigned(c - '0')) / 10, "Integer overflow: " + key);
    value = value * 10 + unsigned(c - '0');
  }
  return value;
}
uint64_t jsonNumber(const QJsonObject& row, const char* key, uint64_t maximum = UINT32_MAX) {
  const auto value = row.value(QLatin1String(key));
  const double n = value.toDouble(-1);
  require(value.isDouble() && std::isfinite(n) && n >= 0 && std::floor(n) == n &&
          n <= double(maximum), std::string("Invalid index integer: ") + key);
  return uint64_t(n);
}
Fields manifest(const std::filesystem::path& root) {
  auto bytes = readBounded(root / "dataset.info", 1024 * 1024);
  if (bytes.startsWith("\xef\xbb\xbf")) bytes.remove(0, 3);
  Fields fields;
  if (bytes.trimmed().startsWith('{')) {
    // Read the early on-device JSON manifest; new recordings use text only.
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(bytes, &error);
    require(error.error == QJsonParseError::NoError && doc.isObject(), "Invalid legacy RAW12 manifest");
    const auto o = doc.object();
    for (auto it = o.begin(); it != o.end(); ++it) {
      if (it.value().isArray()) {
        const auto values = it.value().toArray();
        require(values.size() == 4, "Invalid camera array");
        for (int c = 0; c < 4; ++c) {
          QJsonObject item; item.insert("n", values[c]);
          fields["camera" + std::to_string(c) + "_" + (it.key() == "frames" ? "images" : it.key().toStdString())] =
              std::to_string(jsonNumber(item, "n", 9007199254740991ULL));
        }
      } else if (it.value().isString()) fields[it.key().toStdString()] = it.value().toString().toStdString();
      else if (it.value().isBool()) fields[it.key().toStdString()] = it.value().toBool() ? "1" : "0";
      else if (it.value().isDouble()) fields[it.key().toStdString()] =
          std::to_string(jsonNumber(o, it.key().toUtf8().constData(), 9007199254740991ULL));
    }
    require(number(fields, "schema") == 1, "Unsupported RAW12 schema");
    fields["format"] = "prism-raw12-dataset-v1";
  } else {
    for (const auto& raw : bytes.split('\n')) {
      const auto line = raw.trimmed();
      if (line.isEmpty() || line.startsWith('#')) continue;
      const int at = line.indexOf('=');
      require(at > 0, "Invalid dataset.info line");
      require(fields.emplace(line.left(at).trimmed().toStdString(), line.mid(at+1).trimmed().toStdString()).second,
              "Duplicate manifest field");
    }
  }
  require(fields["format"] == "prism-raw12-dataset-v1" && fields["codec"] == "raw12-rice-lossless",
          "Unsupported RAW12 dataset format");
  require(number(fields, "width") == 5120 && number(fields, "height") == 3072 &&
          number(fields, "source_bits") == 12 && number(fields, "camera_count") == 4,
          "Unsupported RAW12 geometry");
  return fields;
}
std::vector<uint8_t> readFrame(const DatasetImageEntry& entry) {
  const auto path = common::toFilesystemPath(entry.absolute_path);
  regularLocal(path);
  require(entry.byte_size >= 65 && entry.byte_size <= prism::capture::kProfileImageLimit,
          "Invalid RAW12 frame size");
  std::ifstream file(path, std::ios::binary);
  file.seekg(std::streamoff(entry.byte_offset));
  std::vector<uint8_t> bytes(entry.byte_size);
  file.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size()));
  require(size_t(file.gcount()) == bytes.size(), "Truncated RAW12 frame");
  return bytes;
}
prism::capture::Raw12Image decode(const DatasetImageEntry& entry) {
  const auto bytes = readFrame(entry);
  auto raw = prism::capture::decodeRaw12Rice(bytes); // Both encoded and decoded CRCs.
  require(raw.camera == entry.camera && raw.frame_id == entry.frame_id &&
          raw.source_timestamp_us == entry.timestamp_us && raw.timing_mode == entry.timing_mode &&
          raw.exposure_us == entry.exposure_us && raw.gain_x1024 == entry.gain_x1024 &&
          prism::capture::rawLe32(bytes.data()+20) == entry.raw_crc32 &&
          prism::capture::rawLe32(bytes.data()+48) == entry.encoded_crc32,
          "RAW12 payload/index mismatch");
  return raw;
}
}

static Raw12Dataset loadRaw12DatasetImpl(const std::filesystem::path& root,
                             const DatasetValidationCancelCallback& cancelled, bool unpublished) {
  const auto fields = manifest(root);
  Raw12Dataset result;
  unsigned dataset_timing=0;
  const auto complete = number(fields, "complete");
  require(complete == (unpublished ? 0u : 1u), "RAW12 recording completion state mismatch");
  if(!unpublished&&fields.count("companion_streams")&&number(fields,"companion_streams")){
    const auto auxiliary=readBounded(root/"streams.info",1024*1024);
    require(auxiliary.split('\n').count("complete=1")==1&&
            auxiliary.split('\n').count("complete=0")==0,"Auxiliary stream recording incomplete");
  }
  result.complete = complete == 1;
  const bool accounted = fields.count("drop_accounting") != 0;
  const bool allow_absent=fields.count("allow_absent_cameras")&&number(fields,"allow_absent_cameras")==1;
  if (accounted) require(number(fields, "drop_accounting") == 1, "Unsupported drop accounting");
  else for (const auto& field : fields)
    require(field.first != "has_drops" && field.first != "frames_complete" &&
            field.first.find("_dropped") == std::string::npos &&
            field.first.find("received") == std::string::npos, "Missing drop accounting version");
  uint64_t totalDrops = 0;
  const auto fps = number(fields, "fps_per_camera");
  require(fps > 0 && fps <= 120, "Invalid recording rate");
  for (unsigned c = 0; c < 4; ++c) {
    checkCancelled(cancelled);
    const auto prefix = "camera" + std::to_string(c);
    const auto file = root / (prefix + ".prg-stream");
    regularLocal(file); regularLocal(root / (prefix + ".jsonl"));
    const uint64_t size = std::filesystem::file_size(file);
    QFile index(common::fromFilesystemPath(root / (prefix + ".jsonl")));
    require(index.open(QIODevice::ReadOnly), "Cannot read " + prefix + " index");
    std::ifstream data(file, std::ios::binary);
    uint64_t offset = 0, beforeTotal = 0;
    const auto received = accounted ? number(fields, prefix + "_received") : 0;
    const auto dropped = accounted ? number(fields, prefix + "_dropped") : 0;
    const auto tail = accounted ? number(fields, prefix + "_dropped_tail") : 0;
    const auto first = accounted ? number(fields, prefix + "_first_received_frame") : 0;
    const auto final = accounted ? number(fields, prefix + "_last_received_frame") : 0;
    if (accounted) require((received > 0 || (allow_absent&&!size&&!dropped&&!tail&&!first&&!final)) && received <= UINT32_MAX && dropped <= received &&
        tail <= dropped && first <= UINT32_MAX && final <= UINT32_MAX &&
        (!received||uint32_t(first + received - 1) == final), "Invalid drop accounting for " + prefix);
    auto& entries = result.cameras[c];
    while (!index.atEnd()) {
      checkCancelled(cancelled);
      const auto line = index.readLine(4097);
      require(!line.isEmpty() && line.size() <= 4096 && index.error() == QFile::NoError, "Invalid/oversized index row");
      QJsonParseError error;
      const auto doc = QJsonDocument::fromJson(line, &error);
      require(error.error == QJsonParseError::NoError && doc.isObject(), "Invalid index JSON");
      const auto row = doc.object();
      DatasetImageEntry entry;
      entry.raw12 = true; entry.camera = uint8_t(c);
      entry.absolute_path = common::fromFilesystemPath(file);
      entry.byte_offset = jsonNumber(row, "offset", 9007199254740991ULL);
      entry.byte_size = uint32_t(jsonNumber(row, "bytes"));
      entry.frame_id = uint32_t(jsonNumber(row, "frame_id"));
      entry.timestamp_us = jsonNumber(row, "timestamp_us", 9007199254740991ULL);
      entry.timing_mode = uint8_t(jsonNumber(row, "timing_mode", 4));
      require((entry.timing_mode==0||entry.timing_mode==3||entry.timing_mode==4)&&
              prism::capture::validProfileTiming(entry.timing_mode,entry.timestamp_us),"Invalid image timing semantics");
      if(!entry.timing_mode)result.ordinal_only=true;
      else if(!dataset_timing)dataset_timing=entry.timing_mode;
      else if(dataset_timing!=entry.timing_mode)result.ordinal_only=true;
      entry.raw_crc32 = uint32_t(jsonNumber(row, "raw_crc32"));
      entry.encoded_crc32 = uint32_t(jsonNumber(row, "encoded_crc32"));
      entry.dropped_before = accounted || row.contains("dropped_before") ? jsonNumber(row, "dropped_before") : 0;
      require(!entry.dropped_before || (accounted && row.value("drop_reason").toString() == "disk_queue_full"),
              "Unaccounted recording gap");
      require(entry.byte_offset == offset && entry.byte_size >= 65 && entry.byte_size <= prism::capture::kProfileImageLimit &&
              offset <= size && entry.byte_size <= size - offset, "Invalid frame range for " + prefix);
      require(entries.empty() ? (!accounted || entry.frame_id == uint32_t(first + entry.dropped_before)) :
              (entry.frame_id == uint32_t(uint64_t(entries.back().frame_id)+1+entry.dropped_before) &&
               (!entry.timing_mode || entry.timing_mode!=entries.back().timing_mode ||
                entry.timestamp_us > entries.back().timestamp_us)), "Frame sequence/timestamp mismatch for " + prefix);
      // Opening reads only fixed-size headers; payload CRCs are checked on display
      // and all payloads are checked by the cancellable Validate action.
      uint8_t h[64]{};
      data.seekg(std::streamoff(offset)); data.read(reinterpret_cast<char*>(h), 64);
      require(data.gcount() == 64 && !std::memcmp(h, "PRG1", 4) && h[4] == 1 && h[5] == 64 &&
              h[6] == 12 && h[7] <= 3 && h[36] == 2 && h[37] == c && h[38] == entry.timing_mode && h[39] == 0 &&
              prism::capture::rawLe32(h+8) == 5120 && prism::capture::rawLe32(h+12) == 3072 &&
              prism::capture::rawLe32(h+16) == 23592960 && prism::capture::rawLe32(h+20) == entry.raw_crc32 &&
              prism::capture::rawLe32(h+24) == entry.frame_id &&
              (prism::capture::rawLe32(h+40) | (uint64_t(prism::capture::rawLe32(h+44)) << 32)) == entry.timestamp_us &&
              prism::capture::rawLe32(h+48) == entry.encoded_crc32 && prism::capture::rawLe32(h+52) == 256 &&
              prism::capture::rawLe32(h+56) == entry.byte_size - 64 && prism::capture::rawLe32(h+60) == 0,
              "RAW12 header/index mismatch for " + prefix);
      entry.exposure_us = prism::capture::rawLe32(h+28); entry.gain_x1024 = prism::capture::rawLe32(h+32);
      require(result.timestamps.size() < 1000000, "Dataset index exceeds playback limit (one million images)");
      entries.push_back(entry); result.timestamps.push_back(entry.timestamp_us);
      offset += entry.byte_size; beforeTotal += entry.dropped_before;
      if (accounted) require(entries.size() <= received && beforeTotal <= dropped, "Excess frames or drops");
    }
    require(index.error() == QFile::NoError && offset == size && number(fields, prefix + "_images") == entries.size(),
            "Camera file/count mismatch for " + prefix);
    if (accounted) require(entries.size() == received - dropped && beforeTotal + tail == dropped &&
        (entries.empty() || uint32_t(uint64_t(entries.back().frame_id) + tail) == final), "Manifest drop mismatch");
    else require(!entries.empty(), "Missing camera frames");
    result.bytes += size; result.dropped[c] = dropped; totalDrops += dropped;
  }
  require(number(fields, "bytes") == result.bytes, "Manifest byte count mismatch");
  if (accounted) require(number(fields, "has_drops") == (totalDrops != 0) &&
      number(fields, "frames_complete") == (totalDrops == 0), "Manifest drop status mismatch");
  if(fields.count("dropped_frames_total"))require(number(fields,"dropped_frames_total")==totalDrops,
      "Manifest total drop count mismatch");
  if(result.ordinal_only) {
    // No time interpolation or substitution. Each camera's ordinal N is a
    // real entry, not an assertion that four ordinal-N frames were synchronized.
    size_t count=0;for(const auto& lane:result.cameras)count=std::max(count,lane.size());
    result.timestamps.assign(count,0);
    for(size_t n=0;n<count;++n){std::array<int,4> row{};
      for(size_t c=0;c<4;++c)row[c]=n<result.cameras[c].size()?int(n):-1;
      result.display.push_back(row);
    }
    return result;
  }
  std::sort(result.timestamps.begin(), result.timestamps.end());
  result.timestamps.erase(std::unique(result.timestamps.begin(), result.timestamps.end()), result.timestamps.end());
  std::array<size_t, 4> next{};
  std::array<int, 4> current{{-1, -1, -1, -1}};
  for (auto t : result.timestamps) {
    auto display = current;
    for (size_t c = 0; c < 4; ++c) {
      const auto& entries = result.cameras[c];
      while (next[c] < entries.size() && entries[next[c]].timestamp_us <= t) current[c] = int(next[c]++);
      display[c] = current[c];
      if (current[c] >= 0 && t - entries[size_t(current[c])].timestamp_us > 1500000 / fps)
        display[c] = -1; // Never let a lost stream silently freeze forever.
    }
    result.display.push_back(display);
  }
  return result;
}

Raw12Dataset loadRaw12Dataset(const std::filesystem::path& root,
                             const DatasetValidationCancelCallback& cancelled) {
  return loadRaw12DatasetImpl(root, cancelled, false);
}

QImage loadRaw12DatasetImage(const DatasetImageEntry& entry) {
  const auto raw = decode(entry);
  QImage image(int(raw.width), int(raw.height), QImage::Format_Grayscale8);
  require(!image.isNull(), "Cannot allocate RAW12 display image");
  for (unsigned y = 0; y < raw.height; ++y) {
    auto* out = image.scanLine(int(y));
    for (unsigned x = 0; x < raw.width; ++x)
      out[x] = uint8_t(prism::capture::raw12Pixel(raw, size_t(y)*raw.width+x) >> 4);
  }
  // Same display-only provenance as live USB preview. Files and full decoded
  // samples remain untouched; UI controls may filter only their scaled view.
  image.setText(ui::kRaw12PreviewKey, QStringLiteral("native-bayer"));
  return image;
}

static DatasetValidationResult validateRaw12DatasetImpl(const std::filesystem::path& root,
    const DatasetValidationProgressCallback& progress, const DatasetValidationCancelCallback& cancelled,
    bool unpublished) {
  DatasetValidationResult result; result.format_name = "prism-raw12-dataset-v1";
  result.recording_mode = "RAW12 lossless"; result.time_domain = "diagnostic-not-synchronized-exposure";
  try {
    result.device_model = readDatasetIdentity(root).model;
    const auto data = loadRaw12DatasetImpl(root, cancelled, unpublished);
    bool common=false;for(const auto& lane:data.cameras)if(!lane.empty()){common=lane.front().timing_mode==4;break;}
    result.time_domain=data.ordinal_only?"unknown-or-mixed-image-clocks":common?
        "common-external-trigger-device-clock":"diagnostic-not-synchronized-exposure";
    result.cameras_present = true;
    for (size_t c = 0; c < 4; ++c) {
      const auto name = "camera" + std::to_string(c);
      auto& summary = result.cameras[c];
      std::vector<uint64_t> intervals;
      for (const auto& entry : data.cameras[c]) {
        checkCancelled(cancelled); (void)decode(entry);
        if (summary.rows && !data.ordinal_only) intervals.push_back(entry.timestamp_us - summary.last_timestamp_us);
        else summary.first_timestamp_us = entry.timestamp_us;
        summary.last_timestamp_us = entry.timestamp_us; ++summary.rows; ++result.checked_records;
        if (progress) progress(result.checked_records, name);
      }
      if (!intervals.empty()) {
        std::sort(intervals.begin(), intervals.end());
        summary.minimum_interval_us = intervals.front(); summary.maximum_interval_us = intervals.back();
        summary.median_interval_us = intervals[intervals.size()/2];
      }
      if (data.dropped[c]) {
        ++result.total_warnings;
        result.issues.push_back({DatasetValidationSeverity::Warning, name, 0,
            std::to_string(data.dropped[c]) + " recorded queue drops (accounted; no replacement frames)"});
      }
    }
    ++result.total_warnings;
    result.issues.push_back({DatasetValidationSeverity::Warning, "dataset.info", 0,
        data.ordinal_only?"Unknown/mixed image timing: original frames retained for ordinal browsing, no timed playback or synchronization claim.":
        common?"Common physical trigger timestamps on the device clock. UTC synchronization is not asserted by this image flag.":
        "Diagnostic sample timing; not synchronized exposure UTC. Independent camera counts are retained."});
    result.valid = true;
  } catch (const std::exception& e) {
    result.cancelled = cancelled && cancelled();
    if (!result.cancelled) {
      ++result.total_errors;
      result.issues.push_back({DatasetValidationSeverity::Error, "RAW12", 0, e.what()});
    }
  }
  return result;
}
DatasetValidationResult validateRaw12Dataset(const std::filesystem::path& root,
    const DatasetValidationProgressCallback& progress, const DatasetValidationCancelCallback& cancelled) {
  return validateRaw12DatasetImpl(root, progress, cancelled, false);
}
DatasetValidationResult verifyUnpublishedRaw12Dataset(const std::filesystem::path& root) {
  return validateRaw12DatasetImpl(root, {}, {}, true);
}
}  // namespace prism_viewer::dataset
