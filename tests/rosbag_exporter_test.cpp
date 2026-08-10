#include "dataset/rosbag_exporter.hpp"

#include <QtCore/QByteArray>
#include <QtCore/QCoreApplication>
#include <QtCore/QString>
#include <QtCore/QUuid>
#include <QtCore/QVariant>
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlError>
#include <QtSql/QSqlQuery>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<uint8_t>;

uint32_t readU32(const Bytes& bytes, size_t offset) {
  if (offset + 4u > bytes.size()) throw std::runtime_error("short u32");
  uint32_t value = 0;
  for (unsigned byte = 0; byte < 4u; ++byte) {
    value |= static_cast<uint32_t>(bytes[offset + byte]) << (byte * 8u);
  }
  return value;
}

uint64_t readU64(const Bytes& bytes, size_t offset) {
  if (offset + 8u > bytes.size()) throw std::runtime_error("short u64");
  uint64_t value = 0;
  for (unsigned byte = 0; byte < 8u; ++byte) {
    value |= static_cast<uint64_t>(bytes[offset + byte]) << (byte * 8u);
  }
  return value;
}

void appendU32(Bytes* bytes, uint32_t value) {
  for (unsigned byte = 0; byte < 4u; ++byte) {
    bytes->push_back(static_cast<uint8_t>((value >> (byte * 8u)) & 0xffu));
  }
}

struct Record {
  std::map<std::string, Bytes> fields;
  size_t data_offset = 0;
  uint32_t data_size = 0;
  size_t next_offset = 0;
};

std::map<std::string, Bytes> parseFields(const Bytes& bytes, size_t offset,
                                         size_t size) {
  std::map<std::string, Bytes> fields;
  const size_t end = offset + size;
  if (end > bytes.size()) throw std::runtime_error("short field block");
  while (offset < end) {
    const uint32_t field_size = readU32(bytes, offset);
    offset += 4u;
    if (field_size == 0 || offset + field_size > end) {
      throw std::runtime_error("invalid field length");
    }
    size_t delimiter = offset;
    while (delimiter < offset + field_size && bytes[delimiter] != '=') {
      ++delimiter;
    }
    if (delimiter == offset || delimiter == offset + field_size) {
      throw std::runtime_error("invalid field encoding");
    }
    const std::string name(bytes.begin() + static_cast<ptrdiff_t>(offset),
                           bytes.begin() + static_cast<ptrdiff_t>(delimiter));
    fields[name] = Bytes(
        bytes.begin() + static_cast<ptrdiff_t>(delimiter + 1u),
        bytes.begin() + static_cast<ptrdiff_t>(offset + field_size));
    offset += field_size;
  }
  return fields;
}

Record parseRecord(const Bytes& bytes, size_t offset) {
  const uint32_t header_size = readU32(bytes, offset);
  offset += 4u;
  Record record;
  record.fields = parseFields(bytes, offset, header_size);
  offset += header_size;
  record.data_size = readU32(bytes, offset);
  offset += 4u;
  record.data_offset = offset;
  record.next_offset = offset + record.data_size;
  if (record.next_offset > bytes.size()) throw std::runtime_error("short record");
  return record;
}

uint8_t op(const Record& record) {
  const auto found = record.fields.find("op");
  if (found == record.fields.end() || found->second.size() != 1u) {
    throw std::runtime_error("record has no op");
  }
  return found->second[0];
}

uint32_t fieldU32(const Record& record, const std::string& name) {
  const auto found = record.fields.find(name);
  if (found == record.fields.end()) throw std::runtime_error("missing u32 field");
  return readU32(found->second, 0);
}

uint64_t fieldU64(const Record& record, const std::string& name) {
  const auto found = record.fields.find(name);
  if (found == record.fields.end()) throw std::runtime_error("missing u64 field");
  return readU64(found->second, 0);
}

std::string fieldString(const std::map<std::string, Bytes>& fields,
                        const std::string& name) {
  const auto found = fields.find(name);
  if (found == fields.end()) throw std::runtime_error("missing string field");
  return std::string(found->second.begin(), found->second.end());
}

void writeText(const std::filesystem::path& path, const std::string& text) {
  std::ofstream output(path, std::ios::out | std::ios::trunc);
  output << text;
  if (!output.good()) throw std::runtime_error("test text write failed");
}

void writeBytes(const std::filesystem::path& path, const Bytes& bytes) {
  std::ofstream output(path, std::ios::out | std::ios::binary | std::ios::trunc);
  output.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  if (!output.good()) throw std::runtime_error("test binary write failed");
}

Bytes readFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::in | std::ios::binary);
  input.seekg(0, std::ios::end);
  const std::streamoff size = input.tellg();
  input.seekg(0);
  if (size <= 0) throw std::runtime_error("empty test rosbag");
  Bytes bytes(static_cast<size_t>(size));
  input.read(reinterpret_cast<char*>(bytes.data()), size);
  if (input.gcount() != size) throw std::runtime_error("short test rosbag read");
  return bytes;
}

std::string readText(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input.is_open()) throw std::runtime_error("cannot read test text file");
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

class CdrReader {
 public:
  explicit CdrReader(Bytes bytes) : bytes_(std::move(bytes)) {
    if (bytes_.size() < 4u || bytes_[0] != 0u || bytes_[1] != 1u ||
        bytes_[2] != 0u || bytes_[3] != 0u) {
      throw std::runtime_error("invalid little-endian CDR header");
    }
    offset_ = 4u;
  }

  uint8_t readU8() {
    requireBytes(1u);
    return bytes_[offset_++];
  }

  uint32_t readU32() {
    align(4u);
    requireBytes(4u);
    const uint32_t value = ::readU32(bytes_, offset_);
    offset_ += 4u;
    return value;
  }

  int32_t readI32() { return static_cast<int32_t>(readU32()); }

  float readFloat() {
    const uint32_t bits = readU32();
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
  }

  double readDouble() {
    align(8u);
    requireBytes(8u);
    const uint64_t bits = ::readU64(bytes_, offset_);
    offset_ += 8u;
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
  }

  std::string readString() {
    const uint32_t size = readU32();
    if (size == 0u) throw std::runtime_error("CDR string has no terminator");
    requireBytes(size);
    if (bytes_[offset_ + size - 1u] != 0u) {
      throw std::runtime_error("CDR string is not terminated");
    }
    const std::string value(
        bytes_.begin() + static_cast<ptrdiff_t>(offset_),
        bytes_.begin() + static_cast<ptrdiff_t>(offset_ + size - 1u));
    offset_ += size;
    return value;
  }

  Bytes readOctets() {
    const uint32_t size = readU32();
    requireBytes(size);
    Bytes value(bytes_.begin() + static_cast<ptrdiff_t>(offset_),
                bytes_.begin() + static_cast<ptrdiff_t>(offset_ + size));
    offset_ += size;
    return value;
  }

  bool atEnd() const { return offset_ == bytes_.size(); }

 private:
  void align(size_t alignment) {
    const size_t payload_offset = offset_ - 4u;
    offset_ += (alignment - (payload_offset % alignment)) % alignment;
    if (offset_ > bytes_.size()) throw std::runtime_error("short CDR padding");
  }

  void requireBytes(size_t size) const {
    if (offset_ + size > bytes_.size()) {
      throw std::runtime_error("short CDR message");
    }
  }

  Bytes bytes_;
  size_t offset_ = 0;
};

Bytes byteArrayToBytes(const QByteArray& data) {
  return Bytes(reinterpret_cast<const uint8_t*>(data.constData()),
               reinterpret_cast<const uint8_t*>(data.constData()) +
                   data.size());
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() /
      ("prism-rosbag-export-test-" + std::to_string(nonce));
  std::filesystem::create_directories(root);
  try {
    Bytes camera_container;
    for (uint8_t camera = 0; camera < 4u; ++camera) {
      camera_container.insert(camera_container.end(),
                              {0xffu, 0xd8u, camera, 0xffu, 0xd9u});
      writeText(root / ("cam" + std::to_string(camera) + ".tum"),
                "# camera\n1780000000.000500 camera-data-0000.bin " +
                    std::to_string(camera * 5u) + " 5 " +
                    std::to_string(200u + camera * 10u) + "\n");
    }
    writeBytes(root / "camera-data-0000.bin", camera_container);
    writeText(root / "imu0.tum",
              "# imu\n1780000000.000000 0.1 0.2 9.8 0.01 0.02 0.03\n");
    writeText(root / "imu1.tum",
              "# imu\n1780000000.000100 0.4 0.5 9.7 0.04 0.05 0.06\n");

    Bytes lidar_points;
    const std::array<std::array<int32_t, 3>, 2> coordinates = {
        std::array<int32_t, 3>{1000, -2000, 3000},
        std::array<int32_t, 3>{4000, 5000, -6000}};
    for (size_t point = 0; point < coordinates.size(); ++point) {
      for (int32_t coordinate : coordinates[point]) {
        appendU32(&lidar_points, static_cast<uint32_t>(coordinate));
      }
      lidar_points.push_back(static_cast<uint8_t>(70u + point));
      lidar_points.push_back(static_cast<uint8_t>(4u + point));
      lidar_points.push_back(0u);
      lidar_points.push_back(0u);
    }
    writeBytes(root / "lidar-data-0000.bin", lidar_points);
    writeText(root / "lidar.tum",
              "# lidar\n1780000000.000700 lidar-data-0000.bin 0 32 2 2 35 "
              "1 9 1780000000000700000\n");

    const std::filesystem::path bag = root / "dataset.bag";
    const auto result = prism_viewer::dataset::exportDatasetToRosbag(
        root, bag, prism_viewer::dataset::RosbagFormat::Ros1, false);
    if (!result.success || result.camera_messages != 4u ||
        result.imu_messages != 2u || result.lidar_messages != 1u ||
        result.lidar_points != 2u || result.output_bytes == 0u) {
      throw std::runtime_error("export result counts are incorrect: " +
                               result.error);
    }

    const Bytes bytes = readFile(bag);
    const std::string magic(bytes.begin(), bytes.begin() + 13);
    if (magic != "#ROSBAG V2.0\n") throw std::runtime_error("bad bag magic");
    const Record file_header = parseRecord(bytes, 13u);
    if (op(file_header) != 0x03u || fieldU32(file_header, "conn_count") != 7u ||
        fieldU32(file_header, "chunk_count") == 0u) {
      throw std::runtime_error("invalid bag file header");
    }
    const uint64_t index_position = fieldU64(file_header, "index_pos");
    if (index_position <= file_header.next_offset ||
        index_position >= bytes.size()) {
      throw std::runtime_error("invalid bag index position");
    }

    std::map<uint32_t, uint64_t> message_counts;
    uint64_t index_entries = 0;
    size_t offset = file_header.next_offset;
    while (offset < index_position) {
      const Record record = parseRecord(bytes, offset);
      if (op(record) == 0x05u) {
        size_t nested_offset = record.data_offset;
        const size_t nested_end = record.data_offset + record.data_size;
        while (nested_offset < nested_end) {
          const Record nested = parseRecord(bytes, nested_offset);
          if (nested.next_offset > nested_end) {
            throw std::runtime_error("nested record exceeds chunk");
          }
          if (op(nested) == 0x02u) {
            ++message_counts[fieldU32(nested, "conn")];
            if (nested.data_size == 0u) {
              throw std::runtime_error("empty ROS message");
            }
          }
          nested_offset = nested.next_offset;
        }
      } else if (op(record) == 0x04u) {
        index_entries += fieldU32(record, "count");
      } else {
        throw std::runtime_error("unexpected record before bag index");
      }
      offset = record.next_offset;
    }
    if (offset != index_position || index_entries != 7u ||
        message_counts.size() != 7u) {
      throw std::runtime_error("bag chunks or indexes are incomplete");
    }

    std::map<uint32_t, std::string> connection_types;
    uint32_t chunk_infos = 0;
    while (offset < bytes.size()) {
      const Record record = parseRecord(bytes, offset);
      if (op(record) == 0x07u) {
        const uint32_t connection = fieldU32(record, "conn");
        const auto fields =
            parseFields(bytes, record.data_offset, record.data_size);
        connection_types[connection] = fieldString(fields, "type");
        if (fieldString(fields, "topic").find("/prism/") != 0u) {
          throw std::runtime_error("unexpected ROS topic");
        }
      } else if (op(record) == 0x06u) {
        ++chunk_infos;
      } else {
        throw std::runtime_error("unexpected record in bag index section");
      }
      offset = record.next_offset;
    }
    if (connection_types.size() != 7u || chunk_infos == 0u ||
        connection_types[0] != "sensor_msgs/CompressedImage" ||
        connection_types[4] != "sensor_msgs/Imu" ||
        connection_types[6] != "sensor_msgs/PointCloud2") {
      throw std::runtime_error("bag connections are incorrect");
    }

    const std::filesystem::path cancelled_bag = root / "cancelled.bag";
    const auto cancelled = prism_viewer::dataset::exportDatasetToRosbag(
        root, cancelled_bag, prism_viewer::dataset::RosbagFormat::Ros1,
        false, {}, []() { return true; });
    if (!cancelled.cancelled || std::filesystem::exists(cancelled_bag)) {
      throw std::runtime_error("cancelled export left an output file");
    }
    const Bytes original_bag = readFile(bag);
    const auto cancelled_overwrite =
        prism_viewer::dataset::exportDatasetToRosbag(
            root, bag, prism_viewer::dataset::RosbagFormat::Ros1, true, {},
            []() { return true; });
    if (!cancelled_overwrite.cancelled || readFile(bag) != original_bag) {
      throw std::runtime_error(
          "cancelled overwrite changed the existing rosbag");
    }
    const auto overwritten =
        prism_viewer::dataset::exportDatasetToRosbag(
            root, bag, prism_viewer::dataset::RosbagFormat::Ros1, true);
    if (!overwritten.success || readFile(bag).size() != original_bag.size()) {
      throw std::runtime_error("successful rosbag replacement failed");
    }

    const std::filesystem::path ros2_bag = root / "dataset_ros2.rosbag2";
    const auto ros2_result = prism_viewer::dataset::exportDatasetToRosbag(
        root, ros2_bag, prism_viewer::dataset::RosbagFormat::Ros2, false);
    if (!ros2_result.success || ros2_result.camera_messages != 4u ||
        ros2_result.imu_messages != 2u || ros2_result.lidar_messages != 1u ||
        ros2_result.lidar_points != 2u || ros2_result.output_bytes == 0u ||
        !std::filesystem::is_directory(ros2_bag)) {
      throw std::runtime_error("ROS2 export result is incorrect: " +
                               ros2_result.error);
    }
    const std::filesystem::path ros2_database =
        ros2_bag / "dataset_ros2_0.db3";
    const std::filesystem::path ros2_metadata = ros2_bag / "metadata.yaml";
    if (!std::filesystem::is_regular_file(ros2_database) ||
        !std::filesystem::is_regular_file(ros2_metadata)) {
      throw std::runtime_error("ROS2 bag files are missing");
    }
    const std::string metadata = readText(ros2_metadata);
    if (metadata.find("version: 5") == std::string::npos ||
        metadata.find("storage_identifier: sqlite3") == std::string::npos ||
        metadata.find("message_count: 7") == std::string::npos ||
        metadata.find("sensor_msgs/msg/CompressedImage") == std::string::npos ||
        metadata.find("sensor_msgs/msg/Imu") == std::string::npos ||
        metadata.find("sensor_msgs/msg/PointCloud2") == std::string::npos ||
        metadata.find("dataset_ros2_0.db3") == std::string::npos) {
      throw std::runtime_error("ROS2 metadata.yaml is incomplete");
    }

    QByteArray camera_cdr;
    QByteArray imu_cdr;
    QByteArray lidar_cdr;
    const QString sqlite_connection =
        QStringLiteral("prism_rosbag2_test_") +
        QUuid::createUuid().toString(QUuid::WithoutBraces);
    {
      QSqlDatabase database =
          QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                    sqlite_connection);
      database.setDatabaseName(QString::fromStdString(ros2_database.string()));
      if (!database.open()) {
        throw std::runtime_error("cannot open generated ROS2 SQLite bag: " +
                                 database.lastError().text().toStdString());
      }
      QSqlQuery schema(database);
      if (!schema.exec(QStringLiteral(
              "SELECT schema_version, ros_distro FROM schema;")) ||
          !schema.next() || schema.value(0).toInt() != 3 ||
          schema.value(1).toString() != QStringLiteral("humble")) {
        throw std::runtime_error("ROS2 SQLite schema is incorrect");
      }
      QSqlQuery topics(database);
      if (!topics.exec(QStringLiteral(
              "SELECT name, type, serialization_format FROM topics "
              "ORDER BY id;"))) {
        throw std::runtime_error("cannot query ROS2 topics");
      }
      std::map<std::string, std::string> ros2_types;
      while (topics.next()) {
        if (topics.value(2).toString() != QStringLiteral("cdr")) {
          throw std::runtime_error("ROS2 topic is not CDR serialized");
        }
        ros2_types.emplace(topics.value(0).toString().toStdString(),
                           topics.value(1).toString().toStdString());
      }
      if (ros2_types.size() != 7u ||
          ros2_types["/prism/camera0/image/compressed"] !=
              "sensor_msgs/msg/CompressedImage" ||
          ros2_types["/prism/imu0/data"] != "sensor_msgs/msg/Imu" ||
          ros2_types["/prism/lidar/points"] !=
              "sensor_msgs/msg/PointCloud2") {
        throw std::runtime_error("ROS2 topic metadata is incorrect");
      }
      QSqlQuery count(database);
      if (!count.exec(QStringLiteral("SELECT COUNT(*) FROM messages;")) ||
          !count.next() || count.value(0).toULongLong() != 7u) {
        throw std::runtime_error("ROS2 message count is incorrect");
      }
      const auto messageFor = [&database](const QString& topic) {
        QSqlQuery message(database);
        message.prepare(QStringLiteral(
            "SELECT messages.data FROM messages JOIN topics ON "
            "messages.topic_id=topics.id WHERE topics.name=? "
            "ORDER BY messages.timestamp, messages.id LIMIT 1;"));
        message.addBindValue(topic);
        if (!message.exec() || !message.next()) {
          throw std::runtime_error("cannot read ROS2 serialized message");
        }
        return message.value(0).toByteArray();
      };
      camera_cdr =
          messageFor(QStringLiteral("/prism/camera0/image/compressed"));
      imu_cdr = messageFor(QStringLiteral("/prism/imu0/data"));
      lidar_cdr = messageFor(QStringLiteral("/prism/lidar/points"));
      database.close();
    }
    QSqlDatabase::removeDatabase(sqlite_connection);

    const auto nearlyEqual = [](double left, double right) {
      return std::abs(left - right) < 1e-6;
    };
    CdrReader camera_message(byteArrayToBytes(camera_cdr));
    if (camera_message.readI32() != 1780000000 ||
        camera_message.readU32() != 500000u ||
        camera_message.readString() != "camera0_optical_frame" ||
        camera_message.readString() != "bgr8; jpeg compressed bgr8" ||
        camera_message.readOctets() !=
            Bytes({0xffu, 0xd8u, 0u, 0xffu, 0xd9u}) ||
        !camera_message.atEnd()) {
      throw std::runtime_error("ROS2 CompressedImage CDR is incorrect");
    }

    CdrReader imu_message(byteArrayToBytes(imu_cdr));
    if (imu_message.readI32() != 1780000000 ||
        imu_message.readU32() != 0u ||
        imu_message.readString() != "imu0_frame" ||
        !nearlyEqual(imu_message.readDouble(), 0.0) ||
        !nearlyEqual(imu_message.readDouble(), 0.0) ||
        !nearlyEqual(imu_message.readDouble(), 0.0) ||
        !nearlyEqual(imu_message.readDouble(), 1.0)) {
      throw std::runtime_error("ROS2 IMU header or orientation is incorrect");
    }
    for (size_t index = 0; index < 9u; ++index) {
      const double value = imu_message.readDouble();
      if (!nearlyEqual(value, index == 0u ? -1.0 : 0.0)) {
        throw std::runtime_error("ROS2 IMU orientation covariance is incorrect");
      }
    }
    for (double expected : {0.01, 0.02, 0.03}) {
      if (!nearlyEqual(imu_message.readDouble(), expected)) {
        throw std::runtime_error("ROS2 IMU angular velocity is incorrect");
      }
    }
    for (size_t index = 0; index < 9u; ++index) {
      if (!nearlyEqual(imu_message.readDouble(), 0.0)) {
        throw std::runtime_error("ROS2 IMU angular covariance is incorrect");
      }
    }
    for (double expected : {0.1, 0.2, 9.8}) {
      if (!nearlyEqual(imu_message.readDouble(), expected)) {
        throw std::runtime_error("ROS2 IMU acceleration is incorrect");
      }
    }
    for (size_t index = 0; index < 9u; ++index) {
      if (!nearlyEqual(imu_message.readDouble(), 0.0)) {
        throw std::runtime_error("ROS2 IMU acceleration covariance is incorrect");
      }
    }
    if (!imu_message.atEnd()) {
      throw std::runtime_error("ROS2 IMU CDR has trailing data");
    }

    CdrReader lidar_message(byteArrayToBytes(lidar_cdr));
    if (lidar_message.readI32() != 1780000000 ||
        lidar_message.readU32() != 700000u ||
        lidar_message.readString() != "livox_mid360s" ||
        lidar_message.readU32() != 1u || lidar_message.readU32() != 2u ||
        lidar_message.readU32() != 5u) {
      throw std::runtime_error("ROS2 PointCloud2 header is incorrect");
    }
    const std::array<std::string, 5> field_names = {
        "x", "y", "z", "intensity", "tag"};
    const std::array<uint32_t, 5> field_offsets = {0u, 4u, 8u, 12u, 16u};
    const std::array<uint8_t, 5> field_types = {7u, 7u, 7u, 7u, 2u};
    for (size_t field = 0; field < field_names.size(); ++field) {
      if (lidar_message.readString() != field_names[field] ||
          lidar_message.readU32() != field_offsets[field] ||
          lidar_message.readU8() != field_types[field] ||
          lidar_message.readU32() != 1u) {
        throw std::runtime_error("ROS2 PointCloud2 fields are incorrect");
      }
    }
    if (lidar_message.readU8() != 0u || lidar_message.readU32() != 20u ||
        lidar_message.readU32() != 40u) {
      throw std::runtime_error("ROS2 PointCloud2 layout is incorrect");
    }
    const Bytes ros2_points = lidar_message.readOctets();
    const auto pointFloat = [&ros2_points](size_t offset) {
      const uint32_t bits = ::readU32(ros2_points, offset);
      float value = 0.0f;
      std::memcpy(&value, &bits, sizeof(value));
      return value;
    };
    if (ros2_points.size() != 40u || !nearlyEqual(pointFloat(0u), 1.0) ||
        !nearlyEqual(pointFloat(4u), -2.0) ||
        !nearlyEqual(pointFloat(8u), 3.0) ||
        !nearlyEqual(pointFloat(12u), 70.0) || ros2_points[16] != 4u ||
        lidar_message.readU8() != 1u || !lidar_message.atEnd()) {
      throw std::runtime_error("ROS2 PointCloud2 data is incorrect");
    }

    const std::filesystem::path cancelled_ros2 =
        root / "cancelled_ros2.rosbag2";
    const auto ros2_cancelled = prism_viewer::dataset::exportDatasetToRosbag(
        root, cancelled_ros2, prism_viewer::dataset::RosbagFormat::Ros2,
        false, {}, []() { return true; });
    if (!ros2_cancelled.cancelled ||
        std::filesystem::exists(cancelled_ros2)) {
      throw std::runtime_error("cancelled ROS2 export left an output directory");
    }
    const Bytes original_ros2_database = readFile(ros2_database);
    const auto ros2_cancelled_overwrite =
        prism_viewer::dataset::exportDatasetToRosbag(
            root, ros2_bag, prism_viewer::dataset::RosbagFormat::Ros2, true,
            {}, []() { return true; });
    if (!ros2_cancelled_overwrite.cancelled ||
        readFile(ros2_database) != original_ros2_database) {
      throw std::runtime_error(
          "cancelled overwrite changed the existing ROS2 bag");
    }
    const auto ros2_overwritten = prism_viewer::dataset::exportDatasetToRosbag(
        root, ros2_bag, prism_viewer::dataset::RosbagFormat::Ros2, true);
    if (!ros2_overwritten.success ||
        !std::filesystem::is_regular_file(ros2_database)) {
      throw std::runtime_error("successful ROS2 bag replacement failed");
    }

    if (const char* keep = std::getenv("PRISM_ROSBAG_TEST_KEEP");
        keep != nullptr && keep[0] != '\0') {
      std::filesystem::copy_file(
          bag, std::filesystem::path(keep),
          std::filesystem::copy_options::overwrite_existing);
    }
    if (const char* keep = std::getenv("PRISM_ROSBAG2_TEST_KEEP");
        keep != nullptr && keep[0] != '\0') {
      const std::filesystem::path destination(keep);
      std::filesystem::remove_all(destination);
      std::filesystem::copy(
          ros2_bag, destination,
          std::filesystem::copy_options::recursive);
    }
    std::filesystem::remove_all(root);
    std::cout << "ROS1 and ROS2 bag exporter tests passed\n";
    return 0;
  } catch (const std::exception& exception) {
    std::filesystem::remove_all(root);
    std::cerr << exception.what() << '\n';
    return 1;
  }
}
