#include "dataset/rosbag_exporter.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace prism_viewer::dataset {
namespace {

constexpr uint8_t kOpMessageData = 0x02;
constexpr uint8_t kOpFileHeader = 0x03;
constexpr uint8_t kOpIndexData = 0x04;
constexpr uint8_t kOpChunk = 0x05;
constexpr uint8_t kOpChunkInfo = 0x06;
constexpr uint8_t kOpConnection = 0x07;
constexpr uint32_t kFileHeaderLength = 4096;
constexpr uint32_t kChunkTargetBytes = 768u * 1024u;
constexpr uint32_t kMaximumJpegBytes = 64u * 1024u * 1024u;
constexpr uint32_t kMaximumLidarPointsPerBatch = 1000000u;

struct Cancelled final : std::exception {};

using Bytes = std::vector<uint8_t>;

void appendU8(Bytes* bytes, uint8_t value) { bytes->push_back(value); }

void appendU32(Bytes* bytes, uint32_t value) {
  for (unsigned byte = 0; byte < 4; ++byte) {
    bytes->push_back(static_cast<uint8_t>((value >> (8u * byte)) & 0xffu));
  }
}

void appendU64(Bytes* bytes, uint64_t value) {
  for (unsigned byte = 0; byte < 8; ++byte) {
    bytes->push_back(static_cast<uint8_t>((value >> (8u * byte)) & 0xffu));
  }
}

uint32_t readU32(const uint8_t* bytes) {
  uint32_t value = 0;
  for (unsigned byte = 0; byte < 4; ++byte) {
    value |= static_cast<uint32_t>(bytes[byte]) << (8u * byte);
  }
  return value;
}

void appendFloat(Bytes* bytes, float value) {
  static_assert(sizeof(float) == sizeof(uint32_t));
  uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  appendU32(bytes, bits);
}

void appendDouble(Bytes* bytes, double value) {
  static_assert(sizeof(double) == sizeof(uint64_t));
  uint64_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  appendU64(bytes, bits);
}

void appendString(Bytes* bytes, const std::string& value) {
  if (value.size() > std::numeric_limits<uint32_t>::max()) {
    throw std::runtime_error("ROS string is too large");
  }
  appendU32(bytes, static_cast<uint32_t>(value.size()));
  bytes->insert(bytes->end(), value.begin(), value.end());
}

Bytes numberValue(uint8_t value) { return Bytes{value}; }

Bytes numberValue(uint32_t value) {
  Bytes bytes;
  appendU32(&bytes, value);
  return bytes;
}

Bytes numberValue(uint64_t value) {
  Bytes bytes;
  appendU64(&bytes, value);
  return bytes;
}

Bytes stringValue(const std::string& value) {
  return Bytes(value.begin(), value.end());
}

using HeaderFields = std::vector<std::pair<std::string, Bytes>>;

Bytes encodeHeader(const HeaderFields& fields) {
  Bytes header;
  for (const auto& field : fields) {
    if (field.first.empty() ||
        field.first.size() + 1u + field.second.size() >
            std::numeric_limits<uint32_t>::max()) {
      throw std::runtime_error("invalid rosbag header field");
    }
    appendU32(&header, static_cast<uint32_t>(
                           field.first.size() + 1u + field.second.size()));
    header.insert(header.end(), field.first.begin(), field.first.end());
    header.push_back('=');
    header.insert(header.end(), field.second.begin(), field.second.end());
  }
  return header;
}

void appendRecord(Bytes* destination, const HeaderFields& fields,
                  const Bytes& data) {
  const Bytes header = encodeHeader(fields);
  appendU32(destination, static_cast<uint32_t>(header.size()));
  destination->insert(destination->end(), header.begin(), header.end());
  appendU32(destination, static_cast<uint32_t>(data.size()));
  destination->insert(destination->end(), data.begin(), data.end());
}

void writeBytes(std::ofstream* output, const Bytes& bytes) {
  if (!bytes.empty()) {
    output->write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
  }
  if (!output->good()) throw std::runtime_error("rosbag write failed");
}

void writeU32(std::ofstream* output, uint32_t value) {
  Bytes bytes;
  appendU32(&bytes, value);
  writeBytes(output, bytes);
}

void writeRecord(std::ofstream* output, const HeaderFields& fields,
                 const Bytes& data) {
  const Bytes header = encodeHeader(fields);
  writeU32(output, static_cast<uint32_t>(header.size()));
  writeBytes(output, header);
  writeU32(output, static_cast<uint32_t>(data.size()));
  writeBytes(output, data);
}

uint64_t streamPosition(std::ofstream* output) {
  const std::streampos position = output->tellp();
  if (position < 0) throw std::runtime_error("cannot query rosbag position");
  return static_cast<uint64_t>(position);
}

struct RosTime {
  uint32_t seconds = 0;
  uint32_t nanoseconds = 0;
};

RosTime rosTimeFromUs(uint64_t timestamp_us) {
  const uint64_t seconds = timestamp_us / 1000000ULL;
  if (seconds > std::numeric_limits<uint32_t>::max()) {
    throw std::runtime_error("timestamp exceeds ROS1 time range");
  }
  return RosTime{static_cast<uint32_t>(seconds),
                 static_cast<uint32_t>(timestamp_us % 1000000ULL) * 1000u};
}

Bytes timeValue(const RosTime& time) {
  Bytes bytes;
  appendU32(&bytes, time.seconds);
  appendU32(&bytes, time.nanoseconds);
  return bytes;
}

void appendRosHeader(Bytes* message, uint32_t sequence,
                     uint64_t timestamp_us, const std::string& frame_id) {
  const RosTime time = rosTimeFromUs(timestamp_us);
  appendU32(message, sequence);
  appendU32(message, time.seconds);
  appendU32(message, time.nanoseconds);
  appendString(message, frame_id);
}

struct Connection {
  uint32_t id = 0;
  std::string topic;
  std::string type;
  std::string md5;
  std::string definition;
  bool embedded = false;
};

struct IndexEntry {
  RosTime time;
  uint32_t offset = 0;
};

struct ChunkInfo {
  uint64_t position = 0;
  RosTime start;
  RosTime end;
  std::map<uint32_t, uint32_t> connection_counts;
};

uint64_t comparableTime(const RosTime& time) {
  return static_cast<uint64_t>(time.seconds) * 1000000000ULL +
         time.nanoseconds;
}

class RosbagWriter {
 public:
  explicit RosbagWriter(const std::filesystem::path& path) {
    connections_.reserve(8u);
    output_.open(path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!output_.is_open()) throw std::runtime_error("cannot create rosbag");
    static constexpr char kVersion[] = "#ROSBAG V2.0\n";
    output_.write(kVersion, sizeof(kVersion) - 1u);
    file_header_position_ = streamPosition(&output_);
    writeFileHeader(0);
  }

  Connection& addConnection(std::string topic, std::string type,
                            std::string md5, std::string definition) {
    const uint32_t id = static_cast<uint32_t>(connections_.size());
    connections_.push_back(Connection{id, std::move(topic), std::move(type),
                                      std::move(md5), std::move(definition),
                                      false});
    return connections_.back();
  }

  void writeMessage(Connection* connection, uint64_t timestamp_us,
                    const Bytes& message) {
    if (connection == nullptr) throw std::logic_error("missing connection");
    if (!connection->embedded) {
      appendConnectionRecord(&chunk_data_, *connection);
      connection->embedded = true;
    }
    if (chunk_data_.size() > std::numeric_limits<uint32_t>::max()) {
      throw std::runtime_error("rosbag chunk is too large");
    }
    const RosTime time = rosTimeFromUs(timestamp_us);
    const uint32_t offset = static_cast<uint32_t>(chunk_data_.size());
    appendRecord(&chunk_data_,
                 {{"op", numberValue(kOpMessageData)},
                  {"conn", numberValue(connection->id)},
                  {"time", timeValue(time)}},
                 message);
    chunk_indexes_[connection->id].push_back(IndexEntry{time, offset});
    ++chunk_counts_[connection->id];
    if (!chunk_has_messages_) {
      chunk_start_ = time;
      chunk_end_ = time;
      chunk_has_messages_ = true;
    } else {
      if (comparableTime(time) < comparableTime(chunk_start_)) {
        chunk_start_ = time;
      }
      if (comparableTime(time) > comparableTime(chunk_end_)) {
        chunk_end_ = time;
      }
    }
    ++message_count_;
    if (chunk_data_.size() >= kChunkTargetBytes) flushChunk();
  }

  uint64_t finish() {
    flushChunk();
    if (message_count_ == 0) {
      throw std::runtime_error("dataset contains no ROS messages");
    }
    const uint64_t index_position = streamPosition(&output_);
    for (const auto& connection : connections_) {
      writeConnectionRecord(&output_, connection);
    }
    for (const auto& chunk : chunks_) writeChunkInfo(chunk);
    const uint64_t final_size = streamPosition(&output_);
    output_.seekp(static_cast<std::streamoff>(file_header_position_));
    if (!output_.good()) throw std::runtime_error("cannot rewrite rosbag header");
    writeFileHeader(index_position);
    output_.flush();
    if (!output_.good()) throw std::runtime_error("cannot flush rosbag");
    output_.close();
    return final_size;
  }

 private:
  HeaderFields connectionHeader(const Connection& connection) const {
    return {{"topic", stringValue(connection.topic)},
            {"type", stringValue(connection.type)},
            {"md5sum", stringValue(connection.md5)},
            {"message_definition", stringValue(connection.definition)},
            {"callerid", stringValue("/prism_viewer_rosbag_export")},
            {"latching", stringValue("0")}};
  }

  void appendConnectionRecord(Bytes* destination,
                              const Connection& connection) const {
    appendRecord(destination,
                 {{"op", numberValue(kOpConnection)},
                  {"topic", stringValue(connection.topic)},
                  {"conn", numberValue(connection.id)}},
                 encodeHeader(connectionHeader(connection)));
  }

  void writeConnectionRecord(std::ofstream* output,
                             const Connection& connection) const {
    writeRecord(output,
                {{"op", numberValue(kOpConnection)},
                 {"topic", stringValue(connection.topic)},
                 {"conn", numberValue(connection.id)}},
                encodeHeader(connectionHeader(connection)));
  }

  void writeFileHeader(uint64_t index_position) {
    const Bytes header = encodeHeader(
        {{"op", numberValue(kOpFileHeader)},
         {"index_pos", numberValue(index_position)},
         {"conn_count", numberValue(static_cast<uint32_t>(connections_.size()))},
         {"chunk_count", numberValue(static_cast<uint32_t>(chunks_.size()))}});
    if (header.size() > kFileHeaderLength) {
      throw std::runtime_error("rosbag file header is too large");
    }
    writeU32(&output_, static_cast<uint32_t>(header.size()));
    writeBytes(&output_, header);
    const uint32_t padding =
        kFileHeaderLength - static_cast<uint32_t>(header.size());
    writeU32(&output_, padding);
    Bytes spaces(padding, static_cast<uint8_t>(' '));
    writeBytes(&output_, spaces);
  }

  void flushChunk() {
    if (!chunk_has_messages_) return;
    const uint64_t chunk_position = streamPosition(&output_);
    if (chunk_data_.size() > std::numeric_limits<uint32_t>::max()) {
      throw std::runtime_error("rosbag chunk exceeds format limit");
    }
    writeRecord(&output_,
                {{"op", numberValue(kOpChunk)},
                 {"compression", stringValue("none")},
                 {"size", numberValue(static_cast<uint32_t>(chunk_data_.size()))}},
                chunk_data_);
    for (const auto& item : chunk_indexes_) {
      Bytes data;
      data.reserve(item.second.size() * 12u);
      for (const auto& entry : item.second) {
        appendU32(&data, entry.time.seconds);
        appendU32(&data, entry.time.nanoseconds);
        appendU32(&data, entry.offset);
      }
      writeRecord(&output_,
                  {{"op", numberValue(kOpIndexData)},
                   {"conn", numberValue(item.first)},
                   {"ver", numberValue(uint32_t{1})},
                   {"count", numberValue(
                                 static_cast<uint32_t>(item.second.size()))}},
                  data);
    }
    chunks_.push_back(ChunkInfo{chunk_position, chunk_start_, chunk_end_,
                                chunk_counts_});
    chunk_data_.clear();
    chunk_indexes_.clear();
    chunk_counts_.clear();
    chunk_has_messages_ = false;
  }

  void writeChunkInfo(const ChunkInfo& chunk) {
    Bytes data;
    for (const auto& count : chunk.connection_counts) {
      appendU32(&data, count.first);
      appendU32(&data, count.second);
    }
    writeRecord(&output_,
                {{"op", numberValue(kOpChunkInfo)},
                 {"ver", numberValue(uint32_t{1})},
                 {"chunk_pos", numberValue(chunk.position)},
                 {"start_time", timeValue(chunk.start)},
                 {"end_time", timeValue(chunk.end)},
                 {"count", numberValue(static_cast<uint32_t>(
                               chunk.connection_counts.size()))}},
                data);
  }

  std::ofstream output_;
  uint64_t file_header_position_ = 0;
  std::vector<Connection> connections_;
  Bytes chunk_data_;
  std::map<uint32_t, std::vector<IndexEntry>> chunk_indexes_;
  std::map<uint32_t, uint32_t> chunk_counts_;
  std::vector<ChunkInfo> chunks_;
  RosTime chunk_start_;
  RosTime chunk_end_;
  bool chunk_has_messages_ = false;
  uint64_t message_count_ = 0;
};

constexpr const char* kHeaderDefinition =
    "uint32 seq\n"
    "time stamp\n"
    "string frame_id\n";

const std::string kCompressedImageDefinition =
    "std_msgs/Header header\n"
    "string format\n"
    "uint8[] data\n"
    "================================================================================\n"
    "MSG: std_msgs/Header\n" +
    std::string(kHeaderDefinition);

const std::string kImuDefinition =
    "std_msgs/Header header\n"
    "geometry_msgs/Quaternion orientation\n"
    "float64[9] orientation_covariance\n"
    "geometry_msgs/Vector3 angular_velocity\n"
    "float64[9] angular_velocity_covariance\n"
    "geometry_msgs/Vector3 linear_acceleration\n"
    "float64[9] linear_acceleration_covariance\n"
    "================================================================================\n"
    "MSG: std_msgs/Header\n" +
    std::string(kHeaderDefinition) +
    "================================================================================\n"
    "MSG: geometry_msgs/Quaternion\n"
    "float64 x\nfloat64 y\nfloat64 z\nfloat64 w\n"
    "================================================================================\n"
    "MSG: geometry_msgs/Vector3\n"
    "float64 x\nfloat64 y\nfloat64 z\n";

const std::string kPointCloud2Definition =
    "std_msgs/Header header\n"
    "uint32 height\n"
    "uint32 width\n"
    "sensor_msgs/PointField[] fields\n"
    "bool is_bigendian\n"
    "uint32 point_step\n"
    "uint32 row_step\n"
    "uint8[] data\n"
    "bool is_dense\n"
    "================================================================================\n"
    "MSG: std_msgs/Header\n" +
    std::string(kHeaderDefinition) +
    "================================================================================\n"
    "MSG: sensor_msgs/PointField\n"
    "uint8 INT8=1\nuint8 UINT8=2\nuint8 INT16=3\nuint8 UINT16=4\n"
    "uint8 INT32=5\nuint8 UINT32=6\nuint8 FLOAT32=7\nuint8 FLOAT64=8\n"
    "string name\nuint32 offset\nuint8 datatype\nuint32 count\n";

Bytes makeCompressedImage(uint32_t sequence, uint64_t timestamp_us,
                          const std::string& frame_id, const Bytes& jpeg) {
  Bytes message;
  message.reserve(64u + jpeg.size());
  appendRosHeader(&message, sequence, timestamp_us, frame_id);
  appendString(&message, "bgr8; jpeg compressed bgr8");
  appendU32(&message, static_cast<uint32_t>(jpeg.size()));
  message.insert(message.end(), jpeg.begin(), jpeg.end());
  return message;
}

Bytes makeImu(uint32_t sequence, uint64_t timestamp_us,
              const std::string& frame_id,
              const std::array<double, 3>& acceleration,
              const std::array<double, 3>& angular_velocity) {
  Bytes message;
  message.reserve(320u);
  appendRosHeader(&message, sequence, timestamp_us, frame_id);
  appendDouble(&message, 0.0);
  appendDouble(&message, 0.0);
  appendDouble(&message, 0.0);
  appendDouble(&message, 1.0);
  for (size_t index = 0; index < 9; ++index) {
    appendDouble(&message, index == 0 ? -1.0 : 0.0);
  }
  for (double value : angular_velocity) appendDouble(&message, value);
  for (size_t index = 0; index < 9; ++index) appendDouble(&message, 0.0);
  for (double value : acceleration) appendDouble(&message, value);
  for (size_t index = 0; index < 9; ++index) appendDouble(&message, 0.0);
  return message;
}

void appendPointField(Bytes* message, const std::string& name,
                      uint32_t offset, uint8_t datatype) {
  appendString(message, name);
  appendU32(message, offset);
  appendU8(message, datatype);
  appendU32(message, 1u);
}

Bytes makePointCloud2(uint32_t sequence, uint64_t timestamp_us,
                      const std::string& frame_id, const Bytes& point_data,
                      uint32_t point_count) {
  constexpr uint32_t kStoredPointSize = 16u;
  constexpr uint32_t kRosPointSize = 20u;
  if (point_data.size() !=
      static_cast<size_t>(point_count) * kStoredPointSize) {
    throw std::runtime_error("LiDAR payload size does not match point count");
  }
  Bytes message;
  message.reserve(160u + static_cast<size_t>(point_count) * kRosPointSize);
  appendRosHeader(&message, sequence, timestamp_us, frame_id);
  appendU32(&message, 1u);
  appendU32(&message, point_count);
  appendU32(&message, 5u);
  appendPointField(&message, "x", 0u, 7u);
  appendPointField(&message, "y", 4u, 7u);
  appendPointField(&message, "z", 8u, 7u);
  appendPointField(&message, "intensity", 12u, 7u);
  appendPointField(&message, "tag", 16u, 2u);
  appendU8(&message, 0u);
  appendU32(&message, kRosPointSize);
  appendU32(&message, point_count * kRosPointSize);
  appendU32(&message, point_count * kRosPointSize);
  for (uint32_t point = 0; point < point_count; ++point) {
    const uint8_t* source = point_data.data() + point * kStoredPointSize;
    appendFloat(&message, static_cast<int32_t>(readU32(source)) / 1000.0f);
    appendFloat(&message, static_cast<int32_t>(readU32(source + 4)) / 1000.0f);
    appendFloat(&message, static_cast<int32_t>(readU32(source + 8)) / 1000.0f);
    appendFloat(&message, static_cast<float>(source[12]));
    appendU8(&message, source[13]);
    appendU8(&message, 0u);
    appendU8(&message, 0u);
    appendU8(&message, 0u);
  }
  appendU8(&message, 1u);
  return message;
}

bool isSafeRelativePath(const std::filesystem::path& path) {
  if (path.empty() || path.is_absolute()) return false;
  for (const auto& component : path) {
    if (component == std::filesystem::path("..")) return false;
  }
  return true;
}

uint64_t countRows(const std::filesystem::path& path, bool required) {
  std::ifstream input(path);
  if (!input.is_open()) {
    if (required) {
      throw std::runtime_error("cannot open " + path.filename().string());
    }
    return 0;
  }
  uint64_t count = 0;
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line[0] != '#') ++count;
  }
  if (!input.eof()) {
    throw std::runtime_error("failed while reading " + path.filename().string());
  }
  return count;
}

uint64_t parseTimestampUs(const std::string& token) {
  if (token.empty() || token[0] == '-') {
    throw std::runtime_error("invalid negative or empty timestamp");
  }
  const size_t decimal = token.find('.');
  if (decimal != std::string::npos &&
      token.find('.', decimal + 1u) != std::string::npos) {
    throw std::runtime_error("timestamp has multiple decimal points");
  }
  const std::string seconds_text =
      decimal == std::string::npos ? token : token.substr(0, decimal);
  std::string fraction =
      decimal == std::string::npos ? std::string() : token.substr(decimal + 1u);
  const auto decimal_digit = [](char character) {
    return std::isdigit(static_cast<unsigned char>(character)) != 0;
  };
  if (seconds_text.empty() ||
      !std::all_of(seconds_text.begin(), seconds_text.end(), decimal_digit) ||
      !std::all_of(fraction.begin(), fraction.end(), decimal_digit)) {
    throw std::runtime_error("timestamp contains non-decimal characters");
  }
  if (fraction.size() > 6u) fraction.resize(6u);
  while (fraction.size() < 6u) fraction.push_back('0');
  const uint64_t seconds = std::stoull(seconds_text);
  const uint64_t microseconds =
      fraction.empty() ? 0u : std::stoull(fraction);
  if (seconds >
      (std::numeric_limits<uint64_t>::max() - microseconds) / 1000000ULL) {
    throw std::runtime_error("timestamp overflows microseconds");
  }
  return seconds * 1000000ULL + microseconds;
}

Bytes readContainerPayload(const std::filesystem::path& dataset_root,
                           const std::string& relative_text, uint64_t offset,
                           uint32_t size, std::ifstream* container,
                           std::filesystem::path* open_path) {
  const std::filesystem::path relative(relative_text);
  if (!isSafeRelativePath(relative)) {
    throw std::runtime_error("dataset index contains unsafe container path");
  }
  const std::filesystem::path path = (dataset_root / relative).lexically_normal();
  if (*open_path != path) {
    if (container->is_open()) container->close();
    container->clear();
    container->open(path, std::ios::in | std::ios::binary);
    if (!container->is_open()) {
      throw std::runtime_error("cannot open container " + relative_text);
    }
    *open_path = path;
  }
  if (offset > static_cast<uint64_t>(
                   std::numeric_limits<std::streamoff>::max())) {
    throw std::runtime_error("container offset exceeds host file limit");
  }
  container->clear();
  container->seekg(static_cast<std::streamoff>(offset));
  if (!container->good()) {
    throw std::runtime_error("invalid container offset in " + relative_text);
  }
  Bytes payload(size);
  container->read(reinterpret_cast<char*>(payload.data()),
                  static_cast<std::streamsize>(size));
  if (container->gcount() != static_cast<std::streamsize>(size)) {
    throw std::runtime_error("short container read from " + relative_text);
  }
  return payload;
}

void checkCancelled(const RosbagCancelCallback& cancelled) {
  if (cancelled && cancelled()) throw Cancelled();
}

void reportProgress(const RosbagProgressCallback& callback, uint64_t completed,
                    uint64_t total, const std::string& stage, bool force) {
  if (!callback || (!force && completed % 256u != 0u)) return;
  callback(RosbagExportProgress{completed, total, stage});
}

std::filesystem::path temporaryPathFor(const std::filesystem::path& output) {
  const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
  return output.parent_path() /
         (output.filename().string() + ".partial." + std::to_string(nonce));
}

uint64_t lidarTimestampUs(uint32_t time_type, uint64_t raw_timestamp,
                          uint64_t fallback_us) {
  constexpr uint64_t kMinimumPlausibleUnixNanoseconds =
      100000000000000000ULL;
  // Livox encodes PTP as a little-endian uint64 nanosecond timestamp. Other
  // time modes may use a byte-structured GPS representation, so preserve the
  // host reception time unless the packet explicitly reports PTP.
  constexpr uint32_t kLivoxTimestampTypePtp = 1u;
  if (time_type == kLivoxTimestampTypePtp &&
      raw_timestamp >= kMinimumPlausibleUnixNanoseconds) {
    return raw_timestamp / 1000ULL;
  }
  return fallback_us;
}

}  // namespace

RosbagExportResult exportDatasetToRosbag(
    const std::filesystem::path& dataset_root,
    const std::filesystem::path& output_path, bool overwrite,
    const RosbagProgressCallback& progress,
    const RosbagCancelCallback& cancelled) {
  RosbagExportResult result;
  std::filesystem::path temporary;
  try {
    if (dataset_root.empty() || output_path.empty()) {
      throw std::invalid_argument("dataset and output paths are required");
    }
    if (output_path.extension() != std::filesystem::path(".bag")) {
      throw std::invalid_argument("output filename must use the .bag extension");
    }
    if (!std::filesystem::is_directory(dataset_root)) {
      throw std::runtime_error("dataset directory does not exist");
    }
    if (std::filesystem::exists(output_path) && !overwrite) {
      throw std::runtime_error("output rosbag already exists");
    }
    if (std::filesystem::is_directory(output_path)) {
      throw std::runtime_error("output rosbag path is a directory");
    }
    if (!output_path.parent_path().empty() &&
        !std::filesystem::is_directory(output_path.parent_path())) {
      throw std::runtime_error("output directory does not exist");
    }

    std::array<uint64_t, 4> camera_rows{};
    std::array<uint64_t, 2> imu_rows{};
    uint64_t total = 0;
    for (size_t camera = 0; camera < camera_rows.size(); ++camera) {
      camera_rows[camera] = countRows(
          dataset_root / ("cam" + std::to_string(camera) + ".tum"), true);
      total += camera_rows[camera];
    }
    for (size_t imu = 0; imu < imu_rows.size(); ++imu) {
      imu_rows[imu] = countRows(
          dataset_root / ("imu" + std::to_string(imu) + ".tum"), true);
      total += imu_rows[imu];
    }
    const std::filesystem::path lidar_index = dataset_root / "lidar.tum";
    const uint64_t lidar_rows = countRows(lidar_index, false);
    const bool lidar_present = lidar_rows != 0;
    total += lidar_rows;
    if (total == 0) throw std::runtime_error("dataset is empty");

    temporary = temporaryPathFor(output_path);
    RosbagWriter writer(temporary);
    std::array<Connection*, 4> camera_connections{};
    std::array<Connection*, 2> imu_connections{};
    for (size_t camera = 0; camera < camera_connections.size(); ++camera) {
      camera_connections[camera] = &writer.addConnection(
          "/prism/camera" + std::to_string(camera) + "/image/compressed",
          "sensor_msgs/CompressedImage", "8f7a12909da2c9d3332d540a0977563f",
          kCompressedImageDefinition);
    }
    for (size_t imu = 0; imu < imu_connections.size(); ++imu) {
      imu_connections[imu] = &writer.addConnection(
          "/prism/imu" + std::to_string(imu) + "/data", "sensor_msgs/Imu",
          "6a62c6daae103f4ff57a132d6f95cec2", kImuDefinition);
    }
    Connection* lidar_connection = nullptr;
    if (lidar_present) {
      lidar_connection = &writer.addConnection(
          "/prism/lidar/points", "sensor_msgs/PointCloud2",
          "1158d486dd51d683ce2f1be655c3c181", kPointCloud2Definition);
    }

    uint64_t completed = 0;
    reportProgress(progress, completed, total, "Preparing ROS1 bag", true);
    for (size_t camera = 0; camera < camera_connections.size(); ++camera) {
      checkCancelled(cancelled);
      const std::string stage = "Exporting camera " + std::to_string(camera);
      std::ifstream index(
          dataset_root / ("cam" + std::to_string(camera) + ".tum"));
      std::ifstream container;
      std::filesystem::path open_container;
      std::string line;
      uint64_t line_number = 0;
      uint64_t previous_timestamp = 0;
      uint32_t sequence = 0;
      while (std::getline(index, line)) {
        ++line_number;
        if (line.empty() || line[0] == '#') continue;
        std::istringstream parser(line);
        std::string timestamp_text;
        std::string relative_path;
        uint64_t offset = 0;
        uint64_t size = 0;
        uint64_t exposure = 0;
        std::string trailing;
        if (!(parser >> timestamp_text >> relative_path >> offset >> size >>
              exposure) ||
            (parser >> trailing) || size == 0 || size > kMaximumJpegBytes) {
          throw std::runtime_error("invalid cam" + std::to_string(camera) +
                                   ".tum line " +
                                   std::to_string(line_number));
        }
        const uint64_t timestamp_us = parseTimestampUs(timestamp_text);
        if (sequence != 0 && timestamp_us < previous_timestamp) {
          throw std::runtime_error("camera timestamps are not monotonic");
        }
        previous_timestamp = timestamp_us;
        Bytes jpeg = readContainerPayload(
            dataset_root, relative_path, offset, static_cast<uint32_t>(size),
            &container, &open_container);
        writer.writeMessage(
            camera_connections[camera], timestamp_us,
            makeCompressedImage(sequence++, timestamp_us,
                                "camera" + std::to_string(camera) +
                                    "_optical_frame",
                                jpeg));
        ++result.camera_messages;
        ++completed;
        checkCancelled(cancelled);
        reportProgress(progress, completed, total, stage, false);
      }
      if (!index.eof()) throw std::runtime_error("camera index read failed");
      reportProgress(progress, completed, total, stage, true);
    }

    for (size_t imu = 0; imu < imu_connections.size(); ++imu) {
      checkCancelled(cancelled);
      const std::string stage = "Exporting IMU " + std::to_string(imu);
      std::ifstream input(
          dataset_root / ("imu" + std::to_string(imu) + ".tum"));
      std::string line;
      uint64_t line_number = 0;
      uint64_t previous_timestamp = 0;
      uint32_t sequence = 0;
      while (std::getline(input, line)) {
        ++line_number;
        if (line.empty() || line[0] == '#') continue;
        std::istringstream parser(line);
        std::string timestamp_text;
        std::array<double, 3> acceleration{};
        std::array<double, 3> angular_velocity{};
        std::string trailing;
        if (!(parser >> timestamp_text >> acceleration[0] >> acceleration[1] >>
              acceleration[2] >> angular_velocity[0] >> angular_velocity[1] >>
              angular_velocity[2]) ||
            (parser >> trailing)) {
          throw std::runtime_error("invalid imu" + std::to_string(imu) +
                                   ".tum line " +
                                   std::to_string(line_number));
        }
        const uint64_t timestamp_us = parseTimestampUs(timestamp_text);
        if (sequence != 0 && timestamp_us < previous_timestamp) {
          throw std::runtime_error("IMU timestamps are not monotonic");
        }
        previous_timestamp = timestamp_us;
        writer.writeMessage(imu_connections[imu], timestamp_us,
                            makeImu(sequence++, timestamp_us,
                                    "imu" + std::to_string(imu) + "_frame",
                                    acceleration, angular_velocity));
        ++result.imu_messages;
        ++completed;
        checkCancelled(cancelled);
        reportProgress(progress, completed, total, stage, false);
      }
      if (!input.eof()) throw std::runtime_error("IMU index read failed");
      reportProgress(progress, completed, total, stage, true);
    }

    if (lidar_connection != nullptr) {
      checkCancelled(cancelled);
      const std::string stage = "Exporting LiDAR point clouds";
      std::ifstream input(lidar_index);
      std::ifstream container;
      std::filesystem::path open_container;
      std::string line;
      uint64_t line_number = 0;
      uint64_t previous_timestamp = 0;
      uint32_t sequence = 0;
      while (std::getline(input, line)) {
        ++line_number;
        if (line.empty() || line[0] == '#') continue;
        std::istringstream parser(line);
        std::string timestamp_text;
        std::string relative_path;
        uint64_t offset = 0;
        uint64_t size = 0;
        uint64_t point_count = 0;
        uint32_t model = 0;
        uint32_t device_type = 0;
        uint32_t time_type = 0;
        uint32_t batch_id = 0;
        uint64_t raw_timestamp = 0;
        std::string trailing;
        if (!(parser >> timestamp_text >> relative_path >> offset >> size >>
              point_count >> model >> device_type >> time_type >> batch_id >>
              raw_timestamp) ||
            (parser >> trailing) || point_count == 0 ||
            point_count > kMaximumLidarPointsPerBatch ||
            size != point_count * 16ULL || (model != 1u && model != 2u)) {
          throw std::runtime_error("invalid lidar.tum line " +
                                   std::to_string(line_number));
        }
        (void)device_type;
        (void)batch_id;
        const uint64_t fallback_us = parseTimestampUs(timestamp_text);
        const uint64_t timestamp_us =
            lidarTimestampUs(time_type, raw_timestamp, fallback_us);
        if (sequence != 0 && timestamp_us < previous_timestamp) {
          throw std::runtime_error("LiDAR timestamps are not monotonic");
        }
        previous_timestamp = timestamp_us;
        Bytes points = readContainerPayload(
            dataset_root, relative_path, offset, static_cast<uint32_t>(size),
            &container, &open_container);
        writer.writeMessage(
            lidar_connection, timestamp_us,
            makePointCloud2(sequence++, timestamp_us,
                            model == 1u ? "livox_mid360" : "livox_mid360s",
                            points, static_cast<uint32_t>(point_count)));
        ++result.lidar_messages;
        result.lidar_points += point_count;
        ++completed;
        checkCancelled(cancelled);
        reportProgress(progress, completed, total, stage, false);
      }
      if (!input.eof()) throw std::runtime_error("LiDAR index read failed");
      reportProgress(progress, completed, total, stage, true);
    }

    checkCancelled(cancelled);
    reportProgress(progress, completed, total, "Finalizing ROS1 bag", true);
    result.output_bytes = writer.finish();
    checkCancelled(cancelled);
    if (std::filesystem::exists(output_path)) {
      const std::filesystem::path backup = temporaryPathFor(
          output_path.parent_path() /
          (output_path.filename().string() + ".previous"));
      std::filesystem::rename(output_path, backup);
      try {
        std::filesystem::rename(temporary, output_path);
      } catch (...) {
        std::error_code restore_error;
        std::filesystem::rename(backup, output_path, restore_error);
        throw;
      }
      std::error_code ignored;
      std::filesystem::remove(backup, ignored);
    } else {
      std::filesystem::rename(temporary, output_path);
    }
    result.success = true;
    reportProgress(progress, total, total, "ROS1 bag complete", true);
  } catch (const Cancelled&) {
    result.cancelled = true;
  } catch (const std::exception& exception) {
    result.error = exception.what();
  } catch (...) {
    result.error = "unexpected rosbag export error";
  }
  if (!result.success && !temporary.empty()) {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
  }
  return result;
}

}  // namespace prism_viewer::dataset
