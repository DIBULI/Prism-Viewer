#include "dataset/rosbag_exporter.hpp"
#include <QCoreApplication>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QVariant>
#include <array>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>

using namespace prism_viewer::dataset;
using Bytes = std::vector<uint8_t>;
static void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
// Independent wire reader: does not call exporter serializers.
struct Reader {
  const Bytes& bytes;
  size_t pos = 0, origin = 0;
  bool cdr = false;
  uint64_t number(size_t n) {
    if (cdr) pos += (n - (pos - origin) % n) % n;
    require(pos + n <= bytes.size(), "truncated message");
    uint64_t v = 0;
    for (size_t i = 0; i < n; ++i) v |= uint64_t(bytes[pos++]) << (8 * i);
    return v;
  }
  std::string string() {
    const size_t n = number(4);
    require(pos + n <= bytes.size() && (!cdr || n > 0), "bad string");
    std::string s(bytes.begin() + pos, bytes.begin() + pos + n - (cdr ? 1 : 0));
    pos += n; return s;
  }
  double real() { const auto bits = number(8); double v; std::memcpy(&v, &bits, 8); return v; }
};
using Fields = std::map<std::string, Bytes>;
static Fields fields(Reader& r, size_t n) {
  const size_t end = r.pos + n;
  Fields out;
  while (r.pos < end) {
    const auto field = r.string(); const auto equal = field.find('=');
    require(equal != std::string::npos, "bad bag field");
    out[field.substr(0, equal)] = Bytes(field.begin() + equal + 1, field.end());
  }
  require(r.pos == end, "bad bag fields length"); return out;
}
static uint64_t fieldNumber(const Fields& f, const char* key, size_t n) {
  Reader r{f.at(key)}; return r.number(n);
}
static std::string fieldString(const Fields& f, const char* key) {
  const auto& b = f.at(key); return std::string(b.begin(), b.end());
}
struct Message { uint64_t stamp; Bytes bytes; };
using Messages = std::map<std::string, std::vector<Message>>;
static Messages readRos1(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  Bytes bytes(std::istreambuf_iterator<char>(stream), {});
  std::map<uint32_t, std::string> topics;
  std::map<uint32_t, std::vector<Message>> messages;
  const auto scan = [&](const auto& self, const Bytes& data, size_t start) -> void {
    Reader r{data}; r.pos = start;
    while (r.pos < data.size()) {
      const auto n = r.number(4); auto header = fields(r, n);
      const auto size = r.number(4);
      require(r.pos + size <= data.size(), "bad bag record");
      Bytes body(data.begin() + r.pos, data.begin() + r.pos + size); r.pos += size;
      const auto op = fieldNumber(header, "op", 1);
      if (op == 5) { require(fieldString(header, "compression") == "none", "compressed fixture"); self(self, body, 0); }
      if (op == 7) {
        const auto id = uint32_t(fieldNumber(header, "conn", 4));
        topics[id] = fieldString(header, "topic");
        if (topics[id].find("/batch") != std::string::npos) {
          Reader b{body}; auto details = fields(b, body.size());
          require(fieldString(details, "type") == "prism_ros_msgs/BatchIMUMessage", "ROS1 batch type");
          require(fieldString(details, "message_definition").find("uint64[] timestamp_ns") != std::string::npos, "ROS1 definition missing");
        }
      }
      if (op == 2) {
        Reader time{header.at("time")}; auto ns = time.number(4) * 1000000000ULL; ns += time.number(4);
        messages[uint32_t(fieldNumber(header, "conn", 4))].push_back({ns, std::move(body)});
      }
    }
  };
  scan(scan, bytes, 13);
  Messages out;
  for (auto& pair : messages) out[topics.at(pair.first)] = std::move(pair.second);
  return out;
}
static Messages readRos2(const std::filesystem::path& path) {
  Messages out;
  {
    auto db = QSqlDatabase::addDatabase("QSQLITE", "batch-reader");
    db.setDatabaseName(QString::fromStdString((path / "batch_0.db3").string()));
    require(db.open(), "cannot open ROS2 bag");
    QSqlQuery q(db);
    require(q.exec("SELECT topics.name,topics.type,messages.timestamp,messages.data FROM messages JOIN topics "
                   "ON topics.id=messages.topic_id ORDER BY messages.id"), "query ROS2 bag");
    while (q.next()) {
      const auto topic = q.value(0).toString().toStdString();
      if (topic.find("/batch") != std::string::npos)
        require(q.value(1).toString() == "prism_ros_msgs/msg/BatchIMUMessage", "ROS2 batch type");
      auto b = q.value(3).toByteArray();
      out[topic].push_back({q.value(2).toULongLong(), Bytes(b.begin(), b.end())});
    }
    db.close();
  }
  QSqlDatabase::removeDatabase("batch-reader"); return out;
}
struct Sample { uint64_t stamp; std::array<double, 6> values; };
static uint64_t header(Reader& r, const std::string& expected_frame) {
  if (!r.cdr) r.number(4);
  auto ns = r.number(4) * 1000000000ULL; ns += r.number(4);
  require(r.string() == expected_frame, "frame_id changed"); return ns;
}
static void verify(const Messages& messages, bool cdr, size_t sensor, const std::vector<Sample>& expected) {
  const auto prefix = sensor == 2 ? "/prism/lidar/imu" : "/prism/imu" + std::to_string(sensor);
  const auto frame = sensor == 2 ? "livox_imu" : "imu" + std::to_string(sensor) + "_frame";
  const auto& legacy = messages.at(prefix + "/data");
  require(legacy.size() == expected.size(), "legacy count changed");
  std::vector<Sample> expanded;
  for (const auto& msg : messages.at(prefix + "/batch")) {
    Reader r{msg.bytes, cdr ? 4u : 0u, cdr ? 4u : 0u, cdr};
    const auto stamp = header(r, frame), count = r.number(4);
    require(count > 0 && count <= 16, "batch size out of bounds");
    const auto first = expanded.size();
    for (size_t i = 0; i < count; ++i) expanded.push_back({r.number(8), {}});
    require(stamp == expanded[first].stamp && msg.stamp == stamp, "batch header/record timestamp mismatch");
    require(expanded.back().stamp >= stamp && expanded.back().stamp - stamp <= 20000000u, "batch time bound");
    for (size_t field = 0; field < 6; ++field) {
      require(r.number(4) == count, "batch arrays have unequal lengths");
      for (size_t i = 0; i < count; ++i) expanded[first+i].values[field] = r.real();
    }
    require(r.pos == r.bytes.size(), "batch trailing bytes");
  }
  require(expanded.size() == expected.size(), "batch lost or duplicated samples");
  for (size_t i = 0; i < expected.size(); ++i) {
    const auto& e = expected[i];
    require(expanded[i].stamp == e.stamp && expanded[i].values == e.values, "batch sample changed");
    Reader r{legacy[i].bytes, cdr ? 4u : 0u, cdr ? 4u : 0u, cdr};
    require(header(r, frame) == e.stamp && legacy[i].stamp == e.stamp, "legacy timestamp changed");
    for (size_t j = 0; j < 13; ++j) r.real(); // orientation + covariance
    for (size_t j = 3; j < 6; ++j) require(r.real() == e.values[j], "angular velocity differs");
    for (size_t j = 0; j < 9; ++j) r.real();
    for (size_t j = 0; j < 3; ++j) require(r.real() == e.values[j], "acceleration differs");
  }
}
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  try {
    QTemporaryDir temp;
    const std::filesystem::path root = argc == 2 ? argv[1] : temp.path().toStdString();
    std::filesystem::create_directories(root);
    std::ofstream(root / "dataset.info") << "format=prism-dataset-v6\ncomplete=1\nrecording_mode=imu-only\n"
        "image_storage=none\ncamera_index=none\nlidar_storage=none\nlidar_imu_storage=tum-si-v2-with-time-source\n"
        "time_domain=rk-clock-realtime\ntimestamp_epoch=unix\nrecording_stream_policy=all-received\n"
        "timestamp_policy=source-preserved-see-per-stream-flags\nalignment=not-asserted\n";
    std::array<std::vector<Sample>, 3> samples;
    for (size_t sensor = 0; sensor < 3; ++sensor) {
      std::ofstream f(root / (sensor == 2 ? "lidar_imu.tum" : "imu" + std::to_string(sensor) + ".tum"));
      // Full batches, tail, 20ms boundary, gap, regression, duplicate and zero.
      for (size_t i = 0; i < 41; ++i) {
        uint64_t us = 1780000000000000ULL + i * 1000;
        if (i >= 34) us = std::array<uint64_t, 7>{100000,120000,120001,90000,90000,0,1000}[i-34];
        Sample s{us * 1000, {}};
        f << us / 1000000 << '.' << std::setw(6) << std::setfill('0') << us % 1000000;
        for (size_t field = 0; field < 6; ++field) { s.values[field] = double(sensor * 100 + i) + 0.125 * double(field) - 30.5; f << ' ' << std::setprecision(17) << s.values[field]; }
        if (sensor == 2) f << " 2 35 0 " << i << ' ' << s.stamp << " 0 0";
        f << '\n'; samples[sensor].push_back(s);
      }
    }
    for (auto format : {RosbagFormat::Ros1, RosbagFormat::Ros2}) {
      const auto path = root / (format == RosbagFormat::Ros1 ? "batch.bag" : "batch.rosbag2");
      auto result = exportDatasetToRosbag(root, path, format, false);
      require(result.success, result.error.c_str());
      require(result.imu_messages == 82 && result.lidar_imu_messages == 41 &&
              result.imu_batch_messages == 14 && result.lidar_imu_batch_messages == 7, "export sample/batch counts");
      auto messages = format == RosbagFormat::Ros1 ? readRos1(path) : readRos2(path);
      for (size_t sensor = 0; sensor < 3; ++sensor) verify(messages, format == RosbagFormat::Ros2, sensor, samples[sensor]);
    }
    std::cout << "ROS1/ROS2 IMU and BatchIMU preserve every sample, including tails and clock discontinuities\n";
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
