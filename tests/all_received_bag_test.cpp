// Verify original bytes/stamps in both public bag formats, including partial streams.
#include "dataset/rosbag_exporter.cpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QSqlQuery>
#include <iostream>

using namespace prism_viewer::dataset;
static void require(bool ok, const std::string& message) {
  if (!ok) throw std::runtime_error(message);
}
static void text(const std::filesystem::path& path, const std::string& value) {
  std::ofstream stream(path, std::ios::binary); stream << value;
  require(bool(stream), "fixture write failed");
}
static Bytes bytes(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  return Bytes(std::istreambuf_iterator<char>(stream), {});
}
static bool contains(const Bytes& data, const Bytes& expected) {
  return std::search(data.begin(), data.end(), expected.begin(), expected.end()) != data.end();
}
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  try {
    if (argc == 2) {
      const auto layout = readV6DatasetLayout(argv[1]);
      require(layout.all_received, "supplied manifest is not the supported all-received policy");
      std::cout << "Supplied all-received v6 manifest accepted\n";
    }
    QTemporaryDir temporary;
    require(temporary.isValid(), "temporary directory failed");
    const std::filesystem::path root = temporary.path().toStdString();
    const std::string manifest =
        "format=prism-dataset-v6\ncomplete=1\nrecording_mode=full\n"
        "image_storage=chunk-v1\ncamera_index=chunk-v2-with-actual-exposure\n"
        "lidar_storage=cartesian-mm-chunk-v3-with-point-time\n"
        "lidar_imu_storage=tum-si-v2-with-time-source\n"
        "time_domain=rk-clock-realtime\ntimestamp_epoch=unix\n"
        "recording_stream_policy=all-received\n"
        "timestamp_policy=source-preserved-see-per-stream-flags\n"
        "alignment=not-asserted\n";
    text(root / "dataset.info", manifest);
    const Bytes jpeg{0xff,0xd8,0,0xff,0xd9};
    text(root / "camera.bin", std::string(jpeg.begin(), jpeg.end()));
    text(root / "cam0.tum", "1.200000 camera.bin 0 5 200\n1.100000 camera.bin 0 5 250\n");
    text(root / "cam1.tum", "# absent\n"); text(root / "cam2.tum", "# absent\n");
    text(root / "cam3.tum", "0.000000 camera.bin 0 5 300\n");
    text(root / "imu0.tum", "0.000000 1 2 3 4 5 6\n1.300000 1 2 3 4 5 6\n1.100000 1 2 3 4 5 6\n");
    text(root / "imu1.tum", "# absent\n");
    text(root / "points.bin", std::string(16, '\0'));
    text(root / "lidar.tum", "1.500000 points.bin 0 16 1 2 35 0 1 999 0 0 0\n"
                              "0.000000 points.bin 0 16 1 2 35 0 2 888 0 0 0\n");
    text(root / "lidar_imu.tum", "0.000000 1 2 3 4 5 6 2 35 0 1 999 0 0\n");
    for (auto format : {RosbagFormat::Ros1, RosbagFormat::Ros2}) {
      const auto output = root / (format == RosbagFormat::Ros1 ? "received.bag" : "received.rosbag2");
      const auto result = exportDatasetToRosbag(root, output, format, false);
      require(result.success, result.error);
      require(result.synchronization_not_asserted && result.camera_messages == 3 &&
              result.imu_messages == 3 && result.lidar_imu_messages == 1 &&
              result.lidar_points == 2 && result.lidar_messages == 2,
              "partial streams or regression lost samples");
      if (format == RosbagFormat::Ros1) {
        const auto bag = bytes(output);
        const std::string warning = "alignment=not-asserted";
        require(contains(bag, Bytes(warning.begin(), warning.end())), "ROS1 time status missing");
        require(contains(bag, makeRos1CompressedImage(0, 1200000, "camera0_optical_frame", jpeg)) &&
                contains(bag, makeRos1CompressedImage(1, 1100000, "camera0_optical_frame", jpeg)) &&
                contains(bag, makeRos1CompressedImage(0, 0, "camera3_optical_frame", jpeg)),
                "ROS1 image stamps changed");
        for (unsigned i = 0; i < 3; ++i) {
          const uint64_t times[]{0,1300000,1100000};
          require(contains(bag, makeRos1Imu(i, times[i], "imu0_frame", {1,2,3}, {4,5,6})),
                  "ROS1 IMU stamp/value changed");
        }
      } else {
        const QString connection = "received-export-check";
        {
          auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
          db.setDatabaseName(QString::fromStdString((output / "received_0.db3").string()));
          require(db.open(), "ROS2 database open failed");
          QSqlQuery q(db);
          require(q.exec("SELECT timestamp,data FROM messages JOIN topics ON topics.id=messages.topic_id "
                         "WHERE topics.name='/prism/imu0/data' ORDER BY messages.id"), "ROS2 IMU query");
          for (const uint64_t stamp : {0ULL,1300000ULL,1100000ULL}) {
            require(q.next() && q.value(0).toLongLong() == qint64(stamp*1000), "ROS2 record stamp changed");
            const auto data = q.value(1).toByteArray();
            const auto expected = makeRos2Imu(stamp, "imu0_frame", {1,2,3}, {4,5,6});
            require(data == QByteArray(reinterpret_cast<const char*>(expected.data()), int(expected.size())),
                    "ROS2 header stamp/value changed");
          }
          require(!q.next(), "unexpected ROS2 IMU record");
          require(q.exec("SELECT data FROM messages JOIN topics ON topics.id=messages.topic_id "
                         "WHERE topics.name='/prism/recording_info'") && q.next() &&
                  q.value(0).toByteArray().contains("alignment=not-asserted") && !q.next(),
                  "ROS2 time status missing or duplicated");
          db.close();
        }
        QSqlDatabase::removeDatabase(connection);
      }
    }
    // Unconnected streams create empty indexes in the production writer.
    for (const auto* file : {"lidar.tum", "lidar_imu.tum", "imu0.tum"}) text(root/file, "# absent\n");
    for (auto format : {RosbagFormat::Ros1, RosbagFormat::Ros2}) {
      auto result = exportDatasetToRosbag(root, root/(format==RosbagFormat::Ros1?"camera-only.bag":"camera-only.rosbag2"), format, false);
      require(result.success && result.camera_messages == 3 && result.imu_messages == 0 && result.lidar_messages == 0,
              "camera-only export failed: " + result.error);
    }
    // Unknown alignment is still rejected; source policy is not a blanket bypass.
    auto bad = manifest; bad.replace(bad.find("all-received"), 12, "unknown");
    text(root / "dataset.info", bad);
    require(!exportDatasetToRosbag(root, root/"bad.bag", RosbagFormat::Ros1, false).success,
            "unknown time policy accepted");
    text(root / "dataset.info", manifest);
    text(root / "cam0.tum", "1.000000 ../outside.bin 0 5 200\n");
    require(!exportDatasetToRosbag(root, root/"unsafe.bag", RosbagFormat::Ros1, false).success,
            "source-preserved policy bypassed payload path validation");
    std::cout << "PASS ROS1/ROS2 partial streams, original timestamps, clock regressions and provenance\n";
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
