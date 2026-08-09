#include "dataset/rosbag_exporter.hpp"

#include <array>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
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

}  // namespace

int main() {
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
        root, bag, false);
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
        root, cancelled_bag, false, {}, []() { return true; });
    if (!cancelled.cancelled || std::filesystem::exists(cancelled_bag)) {
      throw std::runtime_error("cancelled export left an output file");
    }
    const Bytes original_bag = readFile(bag);
    const auto cancelled_overwrite =
        prism_viewer::dataset::exportDatasetToRosbag(
            root, bag, true, {}, []() { return true; });
    if (!cancelled_overwrite.cancelled || readFile(bag) != original_bag) {
      throw std::runtime_error(
          "cancelled overwrite changed the existing rosbag");
    }
    const auto overwritten =
        prism_viewer::dataset::exportDatasetToRosbag(root, bag, true);
    if (!overwritten.success || readFile(bag).size() != original_bag.size()) {
      throw std::runtime_error("successful rosbag replacement failed");
    }
    if (const char* keep = std::getenv("PRISM_ROSBAG_TEST_KEEP");
        keep != nullptr && keep[0] != '\0') {
      std::filesystem::copy_file(
          bag, std::filesystem::path(keep),
          std::filesystem::copy_options::overwrite_existing);
    }
    std::filesystem::remove_all(root);
    std::cout << "ROS1 bag exporter tests passed\n";
    return 0;
  } catch (const std::exception& exception) {
    std::filesystem::remove_all(root);
    std::cerr << exception.what() << '\n';
    return 1;
  }
}
