#include "dataset/rosbag_exporter.hpp"

#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <QtCore/QUuid>
#include <QtCore/QVariant>
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlError>
#include <QtSql/QSqlQuery>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
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

void appendRos1Header(Bytes* message, uint32_t sequence,
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

class Ros1BagWriter {
 public:
  explicit Ros1BagWriter(const std::filesystem::path& path) {
    connections_.reserve(12u);
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

constexpr const char* kUInt32Definition = "uint32 data\n";

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

Bytes makeRos1CompressedImage(uint32_t sequence, uint64_t timestamp_us,
                              const std::string& frame_id,
                              const Bytes& jpeg) {
  Bytes message;
  message.reserve(64u + jpeg.size());
  appendRos1Header(&message, sequence, timestamp_us, frame_id);
  appendString(&message, "bgr8; jpeg compressed bgr8");
  appendU32(&message, static_cast<uint32_t>(jpeg.size()));
  message.insert(message.end(), jpeg.begin(), jpeg.end());
  return message;
}

Bytes makeRos1UInt32(uint32_t value) {
  Bytes message;
  message.reserve(sizeof(value));
  appendU32(&message, value);
  return message;
}

Bytes makeRos1Imu(uint32_t sequence, uint64_t timestamp_us,
                  const std::string& frame_id,
                  const std::array<double, 3>& acceleration,
                  const std::array<double, 3>& angular_velocity) {
  Bytes message;
  message.reserve(320u);
  appendRos1Header(&message, sequence, timestamp_us, frame_id);
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

Bytes makeRos1PointCloud2(uint32_t sequence, uint64_t timestamp_us,
                          const std::string& frame_id,
                          const Bytes& point_data, uint32_t point_count) {
  constexpr uint32_t kStoredPointSize = 16u;
  constexpr uint32_t kRosPointSize = 20u;
  if (point_data.size() !=
      static_cast<size_t>(point_count) * kStoredPointSize) {
    throw std::runtime_error("LiDAR payload size does not match point count");
  }
  Bytes message;
  message.reserve(160u + static_cast<size_t>(point_count) * kRosPointSize);
  appendRos1Header(&message, sequence, timestamp_us, frame_id);
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

// ROS2 stores messages using little-endian CDR. Alignment is relative to the
// payload after the four-byte encapsulation header (CDR_LE = 0x0001).
class CdrWriter {
 public:
  CdrWriter() : bytes_{0x00u, 0x01u, 0x00u, 0x00u} {}

  void writeU8(uint8_t value) { bytes_.push_back(value); }

  void writeU32(uint32_t value) {
    align(4u);
    appendU32(&bytes_, value);
  }

  void writeI32(int32_t value) {
    writeU32(static_cast<uint32_t>(value));
  }

  void writeFloat(float value) {
    align(4u);
    appendFloat(&bytes_, value);
  }

  void writeDouble(double value) {
    align(8u);
    appendDouble(&bytes_, value);
  }

  void writeString(const std::string& value) {
    if (value.size() >= std::numeric_limits<uint32_t>::max()) {
      throw std::runtime_error("ROS2 string is too large");
    }
    writeU32(static_cast<uint32_t>(value.size() + 1u));
    bytes_.insert(bytes_.end(), value.begin(), value.end());
    bytes_.push_back(0u);
  }

  void writeOctets(const Bytes& value) {
    if (value.size() > std::numeric_limits<uint32_t>::max()) {
      throw std::runtime_error("ROS2 byte sequence is too large");
    }
    writeU32(static_cast<uint32_t>(value.size()));
    bytes_.insert(bytes_.end(), value.begin(), value.end());
  }

  Bytes finish() { return std::move(bytes_); }

 private:
  void align(size_t alignment) {
    const size_t payload_offset = bytes_.size() - 4u;
    const size_t padding =
        (alignment - (payload_offset % alignment)) % alignment;
    bytes_.insert(bytes_.end(), padding, 0u);
  }

  Bytes bytes_;
};

void writeRos2Header(CdrWriter* writer, uint64_t timestamp_us,
                     const std::string& frame_id) {
  const uint64_t seconds = timestamp_us / 1000000ULL;
  if (seconds >
      static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) {
    throw std::runtime_error("timestamp exceeds ROS2 builtin Time range");
  }
  writer->writeI32(static_cast<int32_t>(seconds));
  writer->writeU32(
      static_cast<uint32_t>(timestamp_us % 1000000ULL) * 1000u);
  writer->writeString(frame_id);
}

Bytes makeRos2CompressedImage(uint64_t timestamp_us,
                              const std::string& frame_id,
                              const Bytes& jpeg) {
  CdrWriter writer;
  writeRos2Header(&writer, timestamp_us, frame_id);
  writer.writeString("bgr8; jpeg compressed bgr8");
  writer.writeOctets(jpeg);
  return writer.finish();
}

Bytes makeRos2UInt32(uint32_t value) {
  CdrWriter writer;
  writer.writeU32(value);
  return writer.finish();
}

Bytes makeRos2Imu(uint64_t timestamp_us, const std::string& frame_id,
                  const std::array<double, 3>& acceleration,
                  const std::array<double, 3>& angular_velocity) {
  CdrWriter writer;
  writeRos2Header(&writer, timestamp_us, frame_id);
  writer.writeDouble(0.0);
  writer.writeDouble(0.0);
  writer.writeDouble(0.0);
  writer.writeDouble(1.0);
  for (size_t index = 0; index < 9; ++index) {
    writer.writeDouble(index == 0 ? -1.0 : 0.0);
  }
  for (double value : angular_velocity) writer.writeDouble(value);
  for (size_t index = 0; index < 9; ++index) writer.writeDouble(0.0);
  for (double value : acceleration) writer.writeDouble(value);
  for (size_t index = 0; index < 9; ++index) writer.writeDouble(0.0);
  return writer.finish();
}

void writeRos2PointField(CdrWriter* writer, const std::string& name,
                         uint32_t offset, uint8_t datatype) {
  writer->writeString(name);
  writer->writeU32(offset);
  writer->writeU8(datatype);
  writer->writeU32(1u);
}

Bytes makeRos2PointCloud2(uint64_t timestamp_us,
                          const std::string& frame_id,
                          const Bytes& point_data, uint32_t point_count) {
  constexpr uint32_t kStoredPointSize = 16u;
  constexpr uint32_t kRosPointSize = 20u;
  if (point_data.size() !=
      static_cast<size_t>(point_count) * kStoredPointSize) {
    throw std::runtime_error("LiDAR payload size does not match point count");
  }

  Bytes ros_points;
  ros_points.reserve(static_cast<size_t>(point_count) * kRosPointSize);
  for (uint32_t point = 0; point < point_count; ++point) {
    const uint8_t* source = point_data.data() + point * kStoredPointSize;
    appendFloat(&ros_points,
                static_cast<int32_t>(readU32(source)) / 1000.0f);
    appendFloat(&ros_points,
                static_cast<int32_t>(readU32(source + 4)) / 1000.0f);
    appendFloat(&ros_points,
                static_cast<int32_t>(readU32(source + 8)) / 1000.0f);
    appendFloat(&ros_points, static_cast<float>(source[12]));
    appendU8(&ros_points, source[13]);
    appendU8(&ros_points, 0u);
    appendU8(&ros_points, 0u);
    appendU8(&ros_points, 0u);
  }

  CdrWriter writer;
  writeRos2Header(&writer, timestamp_us, frame_id);
  writer.writeU32(1u);
  writer.writeU32(point_count);
  writer.writeU32(5u);
  writeRos2PointField(&writer, "x", 0u, 7u);
  writeRos2PointField(&writer, "y", 4u, 7u);
  writeRos2PointField(&writer, "z", 8u, 7u);
  writeRos2PointField(&writer, "intensity", 12u, 7u);
  writeRos2PointField(&writer, "tag", 16u, 2u);
  writer.writeU8(0u);
  writer.writeU32(kRosPointSize);
  writer.writeU32(point_count * kRosPointSize);
  writer.writeOctets(ros_points);
  writer.writeU8(1u);
  return writer.finish();
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

uint32_t datasetFormatVersion(const std::filesystem::path& dataset_root) {
  std::ifstream manifest(dataset_root / "dataset.info");
  constexpr const char* kPrefix = "format=prism-dataset-v";
  const size_t prefix_size = std::char_traits<char>::length(kPrefix);
  for (std::string line; std::getline(manifest, line);) {
    if (line.compare(0, prefix_size, kPrefix) != 0) continue;
    const std::string version_text = line.substr(prefix_size);
    if (version_text.empty() ||
        !std::all_of(version_text.begin(), version_text.end(), [](char value) {
          return std::isdigit(static_cast<unsigned char>(value)) != 0;
        })) {
      throw std::runtime_error("invalid dataset format version");
    }
    const unsigned long version = std::stoul(version_text);
    if (version > std::numeric_limits<uint32_t>::max()) {
      throw std::runtime_error("dataset format version is too large");
    }
    return static_cast<uint32_t>(version);
  }
  return 0u;
}

struct V6DatasetLayout {
  bool full = false;
  bool cameras = false;
  bool lidar = false;
  bool lidar_imu = false;
};

V6DatasetLayout readV6DatasetLayout(
    const std::filesystem::path& dataset_root) {
  std::ifstream manifest(dataset_root / "dataset.info");
  if (!manifest.is_open()) {
    throw std::runtime_error("v6 dataset manifest is missing");
  }
  std::map<std::string, std::string> fields;
  for (std::string line; std::getline(manifest, line);) {
    if (line.empty() || line[0] == '#') continue;
    const size_t separator = line.find('=');
    if (separator == std::string::npos || separator == 0u ||
        !fields.emplace(line.substr(0, separator),
                        line.substr(separator + 1u))
             .second) {
      throw std::runtime_error("invalid or duplicate v6 manifest field");
    }
  }
  if (!manifest.eof()) {
    throw std::runtime_error("failed while reading dataset manifest");
  }

  const auto required = [&fields](const char* key) -> const std::string& {
    const auto found = fields.find(key);
    if (found == fields.end()) {
      throw std::runtime_error(std::string("v6 manifest is missing ") + key);
    }
    return found->second;
  };
  if (required("complete") != "1") {
    throw std::runtime_error(
        "v6 dataset is incomplete and cannot be exported");
  }
  if (required("time_domain") != "rk-clock-realtime" ||
      required("timestamp_epoch") != "unix" ||
      required("alignment") != "common-device-time-domain") {
    throw std::runtime_error("v6 dataset has an unsupported time domain");
  }

  V6DatasetLayout layout;
  const std::string& recording_mode = required("recording_mode");
  if (recording_mode == "full") {
    layout.full = true;
  } else if (recording_mode != "imu-only") {
    throw std::runtime_error("v6 dataset has an invalid recording mode");
  }

  const std::string& image_storage = required("image_storage");
  const std::string& camera_index = required("camera_index");
  if (image_storage == "chunk-v1" &&
      camera_index == "chunk-v2-with-actual-exposure") {
    layout.cameras = true;
  } else if (image_storage != "none" || camera_index != "none") {
    throw std::runtime_error("v6 dataset has invalid camera storage fields");
  }

  const std::string& lidar_storage = required("lidar_storage");
  if (lidar_storage == "cartesian-mm-chunk-v2-with-time-source") {
    layout.lidar = true;
  } else if (lidar_storage != "none") {
    throw std::runtime_error("v6 dataset has an invalid LiDAR storage field");
  }
  const std::string& lidar_imu_storage = required("lidar_imu_storage");
  if (lidar_imu_storage == "tum-si-v2-with-time-source") {
    layout.lidar_imu = true;
  } else if (lidar_imu_storage != "none") {
    throw std::runtime_error(
        "v6 dataset has an invalid LiDAR IMU storage field");
  }

  if (layout.full != layout.cameras ||
      (layout.full && layout.lidar != layout.lidar_imu) ||
      (!layout.full && layout.lidar)) {
    throw std::runtime_error(
        "v6 dataset storage declarations contradict its recording mode");
  }
  return layout;
}

bool isPlausibleRkClockRealtimeUs(uint64_t timestamp_us) {
  constexpr uint64_t kMinimumRkClockRealtimeUs = 100000000000000ULL;
  return timestamp_us >= kMinimumRkClockRealtimeUs;
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

QString pathToQString(const std::filesystem::path& path) {
#ifdef _WIN32
  return QString::fromStdWString(path.wstring());
#else
  return QString::fromUtf8(path.string().c_str());
#endif
}

std::string yamlQuote(const std::string& text) {
  std::string quoted = "\"";
  for (const unsigned char character : text) {
    switch (character) {
      case '\\':
        quoted += "\\\\";
        break;
      case '"':
        quoted += "\\\"";
        break;
      case '\n':
        quoted += "\\n";
        break;
      case '\r':
        quoted += "\\r";
        break;
      case '\t':
        quoted += "\\t";
        break;
      default:
        quoted.push_back(static_cast<char>(character));
        break;
    }
  }
  quoted.push_back('"');
  return quoted;
}

struct Ros2Topic {
  int id = 0;
  std::string name;
  std::string type;
  uint64_t message_count = 0;
};

class Ros2BagWriter {
 public:
  Ros2BagWriter(const std::filesystem::path& directory,
                const std::filesystem::path& final_output)
      : directory_(directory),
        connection_name_(QStringLiteral("prism_rosbag2_") +
                         QUuid::createUuid().toString(QUuid::WithoutBraces)) {
    std::string base = final_output.filename().string();
    if (final_output.extension() == std::filesystem::path(".rosbag2")) {
      base = final_output.stem().string();
    }
    if (base.empty()) base = "prism_dataset";
    database_filename_ = base + "_0.db3";
    database_path_ = directory_ / database_filename_;

    if (!std::filesystem::create_directory(directory_)) {
      throw std::runtime_error("cannot create temporary ROS2 bag directory");
    }
    try {
      if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
        throw std::runtime_error(
            "Qt SQLite driver is unavailable; install the Qt SQLite plugin");
      }
      database_ =
          QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection_name_);
      database_.setDatabaseName(pathToQString(database_path_));
      if (!database_.open()) {
        throwSqlError("cannot create ROS2 SQLite bag", database_.lastError());
      }
      execute(QStringLiteral(
          "CREATE TABLE schema(schema_version INTEGER PRIMARY KEY,"
          "ros_distro TEXT NOT NULL);"));
      execute(QStringLiteral(
          "CREATE TABLE metadata(id INTEGER PRIMARY KEY,"
          "metadata_version INTEGER NOT NULL,metadata TEXT NOT NULL);"));
      execute(QStringLiteral(
          "CREATE TABLE topics(id INTEGER PRIMARY KEY,name TEXT NOT NULL,"
          "type TEXT NOT NULL,serialization_format TEXT NOT NULL,"
          "offered_qos_profiles TEXT NOT NULL);"));
      execute(QStringLiteral(
          "CREATE TABLE messages(id INTEGER PRIMARY KEY,topic_id INTEGER "
          "NOT NULL,timestamp INTEGER NOT NULL,data BLOB NOT NULL);"));
      execute(QStringLiteral(
          "CREATE INDEX timestamp_idx ON messages (timestamp ASC);"));

      QSqlQuery schema(database_);
      schema.prepare(QStringLiteral(
          "INSERT INTO schema (schema_version, ros_distro) VALUES (3, ?);"));
      schema.addBindValue(QStringLiteral("humble"));
      if (!schema.exec()) {
        throwSqlError("cannot initialize ROS2 bag schema", schema.lastError());
      }
      if (!database_.transaction()) {
        throwSqlError("cannot start ROS2 bag transaction",
                      database_.lastError());
      }
      transaction_active_ = true;
      insert_message_ = QSqlQuery(database_);
      if (!insert_message_.prepare(QStringLiteral(
              "INSERT INTO messages (timestamp, topic_id, data) "
              "VALUES (?, ?, ?);"))) {
        throwSqlError("cannot prepare ROS2 message insert",
                      insert_message_.lastError());
      }
    } catch (...) {
      closeDatabase();
      throw;
    }
  }

  ~Ros2BagWriter() {
    if (transaction_active_ && database_.isOpen()) database_.rollback();
    closeDatabase();
  }

  int addTopic(const std::string& name, const std::string& type) {
    const int id = static_cast<int>(topics_.size()) + 1;
    QSqlQuery topic(database_);
    topic.prepare(QStringLiteral(
        "INSERT INTO topics (id, name, type, serialization_format, "
        "offered_qos_profiles) VALUES (?, ?, ?, 'cdr', '');"));
    topic.addBindValue(id);
    topic.addBindValue(QString::fromStdString(name));
    topic.addBindValue(QString::fromStdString(type));
    if (!topic.exec()) {
      throwSqlError("cannot add ROS2 bag topic", topic.lastError());
    }
    topics_.push_back(Ros2Topic{id, name, type, 0u});
    return id;
  }

  void writeMessage(int topic_id, uint64_t timestamp_us,
                    const Bytes& message) {
    if (topic_id <= 0 ||
        topic_id > static_cast<int>(topics_.size())) {
      throw std::logic_error("invalid ROS2 bag topic id");
    }
    if (timestamp_us >
        static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) / 1000ULL) {
      throw std::runtime_error("timestamp exceeds ROS2 bag range");
    }
    if (message.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
      throw std::runtime_error("ROS2 message exceeds Qt byte-array limit");
    }
    const int64_t timestamp_ns = static_cast<int64_t>(timestamp_us * 1000ULL);
    const QByteArray payload(
        reinterpret_cast<const char*>(message.data()),
        static_cast<int>(message.size()));
    insert_message_.bindValue(0, static_cast<qlonglong>(timestamp_ns));
    insert_message_.bindValue(1, topic_id);
    insert_message_.bindValue(2, payload);
    if (!insert_message_.exec()) {
      throwSqlError("cannot write ROS2 bag message",
                    insert_message_.lastError());
    }
    ++topics_[static_cast<size_t>(topic_id - 1)].message_count;
    ++message_count_;
    if (!has_messages_) {
      minimum_timestamp_ns_ = timestamp_ns;
      maximum_timestamp_ns_ = timestamp_ns;
      has_messages_ = true;
    } else {
      minimum_timestamp_ns_ = std::min(minimum_timestamp_ns_, timestamp_ns);
      maximum_timestamp_ns_ = std::max(maximum_timestamp_ns_, timestamp_ns);
    }
  }

  uint64_t finish() {
    if (!has_messages_) {
      throw std::runtime_error("dataset contains no ROS messages");
    }
    insert_message_.finish();
    if (!database_.commit()) {
      throwSqlError("cannot commit ROS2 bag", database_.lastError());
    }
    transaction_active_ = false;
    closeDatabase();
    writeMetadata();
    return std::filesystem::file_size(database_path_) +
           std::filesystem::file_size(directory_ / "metadata.yaml");
  }

 private:
  [[noreturn]] static void throwSqlError(const char* operation,
                                         const QSqlError& error) {
    throw std::runtime_error(std::string(operation) + ": " +
                             error.text().toStdString());
  }

  void execute(const QString& statement) {
    QSqlQuery query(database_);
    if (!query.exec(statement)) {
      throwSqlError("cannot initialize ROS2 bag database", query.lastError());
    }
  }

  void closeDatabase() {
    insert_message_ = QSqlQuery();
    if (database_.isValid()) database_.close();
    database_ = QSqlDatabase();
    if (!connection_name_.isEmpty() &&
        QSqlDatabase::contains(connection_name_)) {
      QSqlDatabase::removeDatabase(connection_name_);
    }
  }

  void writeMetadata() const {
    const int64_t duration_ns = maximum_timestamp_ns_ - minimum_timestamp_ns_;
    std::ofstream output(directory_ / "metadata.yaml",
                         std::ios::out | std::ios::trunc);
    if (!output.is_open()) {
      throw std::runtime_error("cannot create ROS2 metadata.yaml");
    }
    output << "rosbag2_bagfile_information:\n"
           << "  version: 5\n"
           << "  storage_identifier: sqlite3\n"
           << "  duration:\n"
           << "    nanoseconds: " << duration_ns << "\n"
           << "  starting_time:\n"
           << "    nanoseconds_since_epoch: " << minimum_timestamp_ns_ << "\n"
           << "  message_count: " << message_count_ << "\n"
           << "  topics_with_message_count:\n";
    for (const auto& topic : topics_) {
      output << "    - topic_metadata:\n"
             << "        name: " << yamlQuote(topic.name) << "\n"
             << "        type: " << yamlQuote(topic.type) << "\n"
             << "        serialization_format: cdr\n"
             << "        offered_qos_profiles: \"\"\n"
             << "      message_count: " << topic.message_count << "\n";
    }
    output << "  compression_format: \"\"\n"
           << "  compression_mode: \"\"\n"
           << "  relative_file_paths:\n"
           << "    - " << yamlQuote(database_filename_) << "\n"
           << "  files:\n"
           << "    - path: " << yamlQuote(database_filename_) << "\n"
           << "      starting_time:\n"
           << "        nanoseconds_since_epoch: " << minimum_timestamp_ns_ << "\n"
           << "      duration:\n"
           << "        nanoseconds: " << duration_ns << "\n"
           << "      message_count: " << message_count_ << "\n";
    output.flush();
    if (!output.good()) {
      throw std::runtime_error("cannot write ROS2 metadata.yaml");
    }
  }

  std::filesystem::path directory_;
  std::filesystem::path database_path_;
  std::string database_filename_;
  QString connection_name_;
  QSqlDatabase database_;
  QSqlQuery insert_message_;
  std::vector<Ros2Topic> topics_;
  uint64_t message_count_ = 0;
  int64_t minimum_timestamp_ns_ = 0;
  int64_t maximum_timestamp_ns_ = 0;
  bool has_messages_ = false;
  bool transaction_active_ = false;
};

class DatasetBagWriter {
 public:
  DatasetBagWriter(RosbagFormat format,
                   const std::filesystem::path& temporary,
                   const std::filesystem::path& final_output,
                   bool cameras_present, bool lidar_present,
                   bool lidar_imu_present)
      : format_(format) {
    if (format_ == RosbagFormat::Ros1) {
      ros1_ = std::make_unique<Ros1BagWriter>(temporary);
      if (cameras_present) {
        for (size_t camera = 0; camera < camera_ros1_.size(); ++camera) {
          camera_ros1_[camera] = &ros1_->addConnection(
              "/prism/camera" + std::to_string(camera) + "/image/compressed",
              "sensor_msgs/CompressedImage",
              "8f7a12909da2c9d3332d540a0977563f",
              kCompressedImageDefinition);
        }
        for (size_t camera = 0; camera < camera_exposure_ros1_.size();
             ++camera) {
          camera_exposure_ros1_[camera] = &ros1_->addConnection(
              "/prism/camera" + std::to_string(camera) + "/exposure_us",
              "std_msgs/UInt32", "304a39449588c7f8ce2df6e8001c5fce",
              kUInt32Definition);
        }
      }
      for (size_t imu = 0; imu < imu_ros1_.size(); ++imu) {
        imu_ros1_[imu] = &ros1_->addConnection(
            "/prism/imu" + std::to_string(imu) + "/data",
            "sensor_msgs/Imu", "6a62c6daae103f4ff57a132d6f95cec2",
            kImuDefinition);
      }
      if (lidar_present) {
        lidar_ros1_ = &ros1_->addConnection(
            "/prism/lidar/points", "sensor_msgs/PointCloud2",
            "1158d486dd51d683ce2f1be655c3c181", kPointCloud2Definition);
      }
      if (lidar_imu_present) {
        lidar_imu_ros1_ = &ros1_->addConnection(
            "/prism/lidar/imu/data", "sensor_msgs/Imu",
            "6a62c6daae103f4ff57a132d6f95cec2", kImuDefinition);
      }
      return;
    }

    ros2_ = std::make_unique<Ros2BagWriter>(temporary, final_output);
    if (cameras_present) {
      for (size_t camera = 0; camera < camera_ros2_.size(); ++camera) {
        camera_ros2_[camera] = ros2_->addTopic(
            "/prism/camera" + std::to_string(camera) + "/image/compressed",
            "sensor_msgs/msg/CompressedImage");
      }
      for (size_t camera = 0; camera < camera_exposure_ros2_.size();
           ++camera) {
        camera_exposure_ros2_[camera] = ros2_->addTopic(
            "/prism/camera" + std::to_string(camera) + "/exposure_us",
            "std_msgs/msg/UInt32");
      }
    }
    for (size_t imu = 0; imu < imu_ros2_.size(); ++imu) {
      imu_ros2_[imu] = ros2_->addTopic(
          "/prism/imu" + std::to_string(imu) + "/data",
          "sensor_msgs/msg/Imu");
    }
    if (lidar_present) {
      lidar_ros2_ = ros2_->addTopic(
          "/prism/lidar/points", "sensor_msgs/msg/PointCloud2");
    }
    if (lidar_imu_present) {
      lidar_imu_ros2_ = ros2_->addTopic(
          "/prism/lidar/imu/data", "sensor_msgs/msg/Imu");
    }
  }

  void writeCamera(size_t camera, uint32_t sequence, uint64_t timestamp_us,
                   uint32_t exposure_us, const Bytes& jpeg) {
    const std::string frame_id =
        "camera" + std::to_string(camera) + "_optical_frame";
    if (format_ == RosbagFormat::Ros1) {
      ros1_->writeMessage(
          camera_ros1_.at(camera), timestamp_us,
          makeRos1CompressedImage(sequence, timestamp_us, frame_id, jpeg));
      ros1_->writeMessage(camera_exposure_ros1_.at(camera), timestamp_us,
                          makeRos1UInt32(exposure_us));
    } else {
      ros2_->writeMessage(
          camera_ros2_.at(camera), timestamp_us,
          makeRos2CompressedImage(timestamp_us, frame_id, jpeg));
      ros2_->writeMessage(camera_exposure_ros2_.at(camera), timestamp_us,
                          makeRos2UInt32(exposure_us));
    }
  }

  void writeImu(size_t imu, uint32_t sequence, uint64_t timestamp_us,
                const std::array<double, 3>& acceleration,
                const std::array<double, 3>& angular_velocity) {
    const std::string frame_id = "imu" + std::to_string(imu) + "_frame";
    if (format_ == RosbagFormat::Ros1) {
      ros1_->writeMessage(
          imu_ros1_.at(imu), timestamp_us,
          makeRos1Imu(sequence, timestamp_us, frame_id, acceleration,
                      angular_velocity));
    } else {
      ros2_->writeMessage(
          imu_ros2_.at(imu), timestamp_us,
          makeRos2Imu(timestamp_us, frame_id, acceleration, angular_velocity));
    }
  }

  void writeLidar(uint32_t sequence, uint64_t timestamp_us,
                  const std::string& frame_id, const Bytes& points,
                  uint32_t point_count) {
    if (format_ == RosbagFormat::Ros1) {
      ros1_->writeMessage(
          lidar_ros1_, timestamp_us,
          makeRos1PointCloud2(sequence, timestamp_us, frame_id, points,
                              point_count));
    } else {
      ros2_->writeMessage(
          lidar_ros2_, timestamp_us,
          makeRos2PointCloud2(timestamp_us, frame_id, points, point_count));
    }
  }

  void writeLidarImu(uint32_t sequence, uint64_t timestamp_us,
                     const std::array<double, 3>& acceleration,
                     const std::array<double, 3>& angular_velocity) {
    constexpr const char* kFrameId = "livox_imu";
    if (format_ == RosbagFormat::Ros1) {
      ros1_->writeMessage(
          lidar_imu_ros1_, timestamp_us,
          makeRos1Imu(sequence, timestamp_us, kFrameId, acceleration,
                      angular_velocity));
    } else {
      ros2_->writeMessage(
          lidar_imu_ros2_, timestamp_us,
          makeRos2Imu(timestamp_us, kFrameId, acceleration,
                      angular_velocity));
    }
  }

  uint64_t finish() {
    return format_ == RosbagFormat::Ros1 ? ros1_->finish() : ros2_->finish();
  }

 private:
  RosbagFormat format_;
  std::unique_ptr<Ros1BagWriter> ros1_;
  std::unique_ptr<Ros2BagWriter> ros2_;
  std::array<Connection*, 4> camera_ros1_{};
  std::array<Connection*, 4> camera_exposure_ros1_{};
  std::array<Connection*, 2> imu_ros1_{};
  Connection* lidar_ros1_ = nullptr;
  Connection* lidar_imu_ros1_ = nullptr;
  std::array<int, 4> camera_ros2_{};
  std::array<int, 4> camera_exposure_ros2_{};
  std::array<int, 2> imu_ros2_{};
  int lidar_ros2_ = 0;
  int lidar_imu_ros2_ = 0;
};

}  // namespace

RosbagExportResult exportDatasetToRosbag(
    const std::filesystem::path& dataset_root,
    const std::filesystem::path& output_path, RosbagFormat format,
    bool overwrite,
    const RosbagProgressCallback& progress,
    const RosbagCancelCallback& cancelled) {
  RosbagExportResult result;
  std::filesystem::path temporary;
  try {
    if (dataset_root.empty() || output_path.empty()) {
      throw std::invalid_argument("dataset and output paths are required");
    }
    if (format != RosbagFormat::Ros1 && format != RosbagFormat::Ros2) {
      throw std::invalid_argument("unsupported ROS bag format");
    }
    if (format == RosbagFormat::Ros1 &&
        output_path.extension() != std::filesystem::path(".bag")) {
      throw std::invalid_argument("output filename must use the .bag extension");
    }
    if (!std::filesystem::is_directory(dataset_root)) {
      throw std::runtime_error("dataset directory does not exist");
    }
    if (std::filesystem::exists(output_path) && !overwrite) {
      throw std::runtime_error("output rosbag already exists");
    }
    if (std::filesystem::exists(output_path) &&
        ((format == RosbagFormat::Ros1 &&
          std::filesystem::is_directory(output_path)) ||
         (format == RosbagFormat::Ros2 &&
          !std::filesystem::is_directory(output_path)))) {
      throw std::runtime_error(
          format == RosbagFormat::Ros1
              ? "ROS1 output path is a directory"
              : "ROS2 output path is not a directory");
    }
    if (!output_path.parent_path().empty() &&
        !std::filesystem::is_directory(output_path.parent_path())) {
      throw std::runtime_error("output directory does not exist");
    }
    if (std::filesystem::weakly_canonical(dataset_root) ==
        std::filesystem::weakly_canonical(output_path)) {
      throw std::runtime_error("output path cannot replace the source dataset");
    }
    const uint32_t dataset_version = datasetFormatVersion(dataset_root);
    if (dataset_version > 6u) {
      throw std::runtime_error("unsupported future Prism dataset format");
    }
    const bool strict_time_v6 = dataset_version == 6u;
    V6DatasetLayout v6_layout;
    if (strict_time_v6) v6_layout = readV6DatasetLayout(dataset_root);

    std::array<uint64_t, 4> camera_rows{};
    std::array<uint64_t, 2> imu_rows{};
    uint64_t total = 0;
    size_t camera_index_count = 0;
    for (size_t camera = 0; camera < camera_rows.size(); ++camera) {
      if (std::filesystem::is_regular_file(
              dataset_root /
              ("cam" + std::to_string(camera) + ".tum"))) {
        ++camera_index_count;
      }
    }
    if (strict_time_v6 &&
        camera_index_count != (v6_layout.cameras ? camera_rows.size() : 0u)) {
      throw std::runtime_error(
          "v6 camera indexes do not match the manifest declaration");
    }
    if (!strict_time_v6 && camera_index_count != 0 &&
        camera_index_count != camera_rows.size()) {
      throw std::runtime_error(
          "dataset has an incomplete set of camera indexes");
    }
    const bool cameras_present =
        strict_time_v6 ? v6_layout.cameras
                       : camera_index_count == camera_rows.size();
    if (cameras_present) {
      for (size_t camera = 0; camera < camera_rows.size(); ++camera) {
        camera_rows[camera] = countRows(
            dataset_root / ("cam" + std::to_string(camera) + ".tum"), true);
        total += camera_rows[camera];
      }
      if (strict_time_v6 &&
          (camera_rows.front() == 0u ||
           !std::all_of(camera_rows.begin(), camera_rows.end(),
                        [&camera_rows](uint64_t rows) {
                          return rows == camera_rows.front();
                        }))) {
        throw std::runtime_error(
            "v6 camera indexes contain no complete synchronized frame sets");
      }
    }
    for (size_t imu = 0; imu < imu_rows.size(); ++imu) {
      imu_rows[imu] = countRows(
          dataset_root / ("imu" + std::to_string(imu) + ".tum"), true);
      if (strict_time_v6 && imu_rows[imu] == 0u) {
        throw std::runtime_error("v6 onboard IMU stream is empty");
      }
      total += imu_rows[imu];
    }
    const std::filesystem::path lidar_index = dataset_root / "lidar.tum";
    const uint64_t lidar_rows = countRows(lidar_index, false);
    const bool lidar_file_present =
        std::filesystem::is_regular_file(lidar_index);
    if (strict_time_v6 &&
        ((v6_layout.lidar && (!lidar_file_present || lidar_rows == 0u)) ||
         (!v6_layout.lidar && lidar_file_present))) {
      throw std::runtime_error(
          "v6 LiDAR point index does not match the manifest declaration");
    }
    const bool lidar_present =
        strict_time_v6 ? v6_layout.lidar : lidar_rows != 0;
    total += lidar_rows;
    const std::filesystem::path lidar_imu_index =
        dataset_root / "lidar_imu.tum";
    const uint64_t lidar_imu_rows = countRows(lidar_imu_index, false);
    const bool lidar_imu_file_present =
        std::filesystem::is_regular_file(lidar_imu_index);
    if (strict_time_v6 &&
        ((v6_layout.lidar_imu &&
          (!lidar_imu_file_present || lidar_imu_rows == 0u)) ||
         (!v6_layout.lidar_imu && lidar_imu_file_present))) {
      throw std::runtime_error(
          "v6 LiDAR IMU index does not match the manifest declaration");
    }
    const bool lidar_imu_present =
        strict_time_v6 ? v6_layout.lidar_imu : lidar_imu_rows != 0;
    total += lidar_imu_rows;
    if (total == 0) throw std::runtime_error("dataset is empty");

    temporary = temporaryPathFor(output_path);
    DatasetBagWriter writer(format, temporary, output_path, cameras_present,
                            lidar_present, lidar_imu_present);

    uint64_t completed = 0;
    std::vector<uint64_t> camera0_timestamps;
    if (strict_time_v6 && cameras_present) {
      camera0_timestamps.reserve(static_cast<size_t>(camera_rows.front()));
    }
    reportProgress(progress, completed, total,
                   format == RosbagFormat::Ros1 ? "Preparing ROS1 bag"
                                                : "Preparing ROS2 bag",
                   true);
    if (cameras_present) {
      for (size_t camera = 0; camera < camera_rows.size(); ++camera) {
        checkCancelled(cancelled);
        const std::string stage =
            "Exporting camera " + std::to_string(camera);
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
              (parser >> trailing) || size == 0 || size > kMaximumJpegBytes ||
              exposure > std::numeric_limits<uint32_t>::max()) {
            throw std::runtime_error("invalid cam" + std::to_string(camera) +
                                     ".tum line " +
                                     std::to_string(line_number));
          }
          const uint64_t timestamp_us = parseTimestampUs(timestamp_text);
          if (strict_time_v6 &&
              !isPlausibleRkClockRealtimeUs(timestamp_us)) {
            throw std::runtime_error(
                "v6 camera timestamp is not in the RK CLOCK_REALTIME epoch");
          }
          if (sequence != 0 && timestamp_us < previous_timestamp) {
            throw std::runtime_error("camera timestamps are not monotonic");
          }
          if (strict_time_v6) {
            if (camera == 0) {
              camera0_timestamps.push_back(timestamp_us);
            } else if (sequence >= camera0_timestamps.size() ||
                       camera0_timestamps[sequence] != timestamp_us) {
              throw std::runtime_error(
                  "v6 four-camera frame timestamps are not identical");
            }
          }
          previous_timestamp = timestamp_us;
          Bytes jpeg = readContainerPayload(
              dataset_root, relative_path, offset, static_cast<uint32_t>(size),
              &container, &open_container);
          writer.writeCamera(camera, sequence++, timestamp_us,
                             static_cast<uint32_t>(exposure), jpeg);
          ++result.camera_messages;
          ++result.camera_exposure_messages;
          ++completed;
          checkCancelled(cancelled);
          reportProgress(progress, completed, total, stage, false);
        }
        if (!index.eof()) {
          throw std::runtime_error("camera index read failed");
        }
        reportProgress(progress, completed, total, stage, true);
      }
    }

    for (size_t imu = 0; imu < imu_rows.size(); ++imu) {
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
        if (strict_time_v6 &&
            !isPlausibleRkClockRealtimeUs(timestamp_us)) {
          throw std::runtime_error(
              "v6 IMU timestamp is not in the RK CLOCK_REALTIME epoch");
        }
        if (sequence != 0 && timestamp_us < previous_timestamp) {
          throw std::runtime_error("IMU timestamps are not monotonic");
        }
        previous_timestamp = timestamp_us;
        writer.writeImu(imu, sequence++, timestamp_us, acceleration,
                        angular_velocity);
        ++result.imu_messages;
        ++completed;
        checkCancelled(cancelled);
        reportProgress(progress, completed, total, stage, false);
      }
      if (!input.eof()) throw std::runtime_error("IMU index read failed");
      reportProgress(progress, completed, total, stage, true);
    }

    if (lidar_present) {
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
        if (!(parser >> timestamp_text >> relative_path >> offset >> size >>
              point_count >> model >> device_type >> time_type >> batch_id >>
              raw_timestamp) || point_count == 0 ||
            point_count > kMaximumLidarPointsPerBatch ||
            size != point_count * 16ULL || (model != 1u && model != 2u)) {
          throw std::runtime_error("invalid lidar.tum line " +
                                   std::to_string(line_number));
        }
        bool has_v6_time_source = false;
        uint32_t time_interval_100ns = 0;
        uint32_t timestamp_synced = 0;
        uint32_t tai_offset_applied = 0;
        parser >> std::ws;
        if (!parser.eof()) {
          std::string trailing;
          if (!(parser >> time_interval_100ns >> timestamp_synced >>
                tai_offset_applied) ||
              (parser >> trailing) ||
              time_interval_100ns >
                  std::numeric_limits<uint16_t>::max() ||
              timestamp_synced > 1u || tai_offset_applied > 1u ||
              (tai_offset_applied != 0u && timestamp_synced == 0u)) {
            throw std::runtime_error("invalid lidar.tum line " +
                                     std::to_string(line_number));
          }
          has_v6_time_source = true;
        }
        if (strict_time_v6 &&
            (!has_v6_time_source || timestamp_synced != 1u)) {
          throw std::runtime_error(
              "v6 lidar.tum requires a synchronized RK time source at line " +
              std::to_string(line_number));
        }
        (void)device_type;
        (void)batch_id;
        (void)time_interval_100ns;
        // v6 records the LiDAR batch time normalized into the RK
        // CLOCK_REALTIME epoch in the first column. Legacy v5 used a host
        // receive time there. In both cases timestamp_raw remains opaque Livox
        // metadata and must not be written directly to ROS.
        const uint64_t timestamp_us = parseTimestampUs(timestamp_text);
        if (strict_time_v6 &&
            !isPlausibleRkClockRealtimeUs(timestamp_us)) {
          throw std::runtime_error(
              "v6 LiDAR timestamp is not in the RK CLOCK_REALTIME epoch");
        }
        (void)time_type;
        (void)raw_timestamp;
        if (sequence != 0 && timestamp_us < previous_timestamp) {
          throw std::runtime_error("LiDAR timestamps are not monotonic");
        }
        previous_timestamp = timestamp_us;
        Bytes points = readContainerPayload(
            dataset_root, relative_path, offset, static_cast<uint32_t>(size),
            &container, &open_container);
        writer.writeLidar(
            sequence++, timestamp_us,
            model == 1u ? "livox_mid360" : "livox_mid360s", points,
            static_cast<uint32_t>(point_count));
        ++result.lidar_messages;
        result.lidar_points += point_count;
        ++completed;
        checkCancelled(cancelled);
        reportProgress(progress, completed, total, stage, false);
      }
      if (!input.eof()) throw std::runtime_error("LiDAR index read failed");
      reportProgress(progress, completed, total, stage, true);
    }

    if (lidar_imu_present) {
      checkCancelled(cancelled);
      const std::string stage = "Exporting LiDAR IMU";
      std::ifstream input(lidar_imu_index);
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
        if (!(parser >> timestamp_text >> acceleration[0] >> acceleration[1] >>
              acceleration[2] >> angular_velocity[0] >> angular_velocity[1] >>
              angular_velocity[2])) {
          throw std::runtime_error("invalid lidar_imu.tum line " +
                                   std::to_string(line_number));
        }
        const bool values_finite =
            std::all_of(acceleration.begin(), acceleration.end(),
                        [](double value) { return std::isfinite(value); }) &&
            std::all_of(angular_velocity.begin(), angular_velocity.end(),
                        [](double value) { return std::isfinite(value); });
        if (!values_finite) {
          throw std::runtime_error("invalid lidar_imu.tum line " +
                                   std::to_string(line_number));
        }

        // v3/v4 compact rows have no provenance; v5 appends six source fields;
        // v6 appends tai_offset_applied as a seventh source field.
        bool has_time_source = false;
        bool has_tai_flag = false;
        uint32_t timestamp_synced = 0;
        uint32_t tai_offset_applied = 0;
        parser >> std::ws;
        if (!parser.eof()) {
          uint32_t model = 0;
          uint32_t device_type = 0;
          uint32_t time_type = 0;
          uint64_t sample_id = 0;
          uint64_t timestamp_raw = 0;
          if (!(parser >> model >> device_type >> time_type >> sample_id >>
                timestamp_raw >> timestamp_synced) ||
              (model != 1u && model != 2u) ||
              device_type > std::numeric_limits<uint8_t>::max() ||
              time_type > std::numeric_limits<uint8_t>::max() ||
              sample_id > std::numeric_limits<uint32_t>::max() ||
              timestamp_synced > 1u) {
            throw std::runtime_error("invalid lidar_imu.tum line " +
                                     std::to_string(line_number));
          }
          has_time_source = true;
          parser >> std::ws;
          if (!parser.eof()) {
            std::string trailing;
            if (!(parser >> tai_offset_applied) || (parser >> trailing) ||
                tai_offset_applied > 1u ||
                (tai_offset_applied != 0u && timestamp_synced == 0u)) {
              throw std::runtime_error("invalid lidar_imu.tum line " +
                                       std::to_string(line_number));
            }
            has_tai_flag = true;
          }
        }
        if (strict_time_v6 &&
            (!has_time_source || !has_tai_flag || timestamp_synced != 1u)) {
          throw std::runtime_error(
              "v6 lidar_imu.tum requires a synchronized RK time source at line " +
              std::to_string(line_number));
        }

        const uint64_t timestamp_us = parseTimestampUs(timestamp_text);
        if (strict_time_v6 &&
            !isPlausibleRkClockRealtimeUs(timestamp_us)) {
          throw std::runtime_error(
              "v6 LiDAR IMU timestamp is not in the RK CLOCK_REALTIME epoch");
        }
        if (sequence != 0 && timestamp_us < previous_timestamp) {
          throw std::runtime_error("LiDAR IMU timestamps are not monotonic");
        }
        previous_timestamp = timestamp_us;
        writer.writeLidarImu(sequence++, timestamp_us, acceleration,
                             angular_velocity);
        ++result.lidar_imu_messages;
        ++completed;
        checkCancelled(cancelled);
        reportProgress(progress, completed, total, stage, false);
      }
      if (!input.eof()) throw std::runtime_error("LiDAR IMU index read failed");
      reportProgress(progress, completed, total, stage, true);
    }

    checkCancelled(cancelled);
    reportProgress(progress, completed, total,
                   format == RosbagFormat::Ros1 ? "Finalizing ROS1 bag"
                                                : "Finalizing ROS2 bag",
                   true);
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
      std::filesystem::remove_all(backup, ignored);
    } else {
      std::filesystem::rename(temporary, output_path);
    }
    result.success = true;
    reportProgress(progress, total, total,
                   format == RosbagFormat::Ros1 ? "ROS1 bag complete"
                                                : "ROS2 bag complete",
                   true);
  } catch (const Cancelled&) {
    result.cancelled = true;
  } catch (const std::exception& exception) {
    result.error = exception.what();
  } catch (...) {
    result.error = "unexpected rosbag export error";
  }
  if (!result.success && !temporary.empty()) {
    std::error_code ignored;
    std::filesystem::remove_all(temporary, ignored);
  }
  return result;
}

}  // namespace prism_viewer::dataset
