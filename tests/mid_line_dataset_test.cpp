// Exercise the real grouping and serializers without widening the public API.
#include "dataset/rosbag_exporter.cpp"
#include "dataset/dataset_playback.hpp"
#include "dataset/dataset_browser.hpp"
#include <QtCore/QCoreApplication>
#include <QtCore/QProcess>
#include <QtCore/QTemporaryDir>
#include <QtGui/QImage>
#include <iostream>

using namespace prism_viewer::dataset;
static void require(bool ok, const char* why) {
  if (!ok) throw std::runtime_error(why);
}

static void checkCloud(const LidarFrame& frame, size_t& next) {
  for (const auto& p : frame.points) {
    require(p.line_valid && p.line == (next % 95u) % 4u,
            "100 ms frame regenerated or lost original UDP line");
    ++next;
  }
  for (const auto& cloud : {makeRos1PointCloud2(0, frame), makeRos2PointCloud2(frame)}) {
    const size_t start = cloud.size() - 1u - frame.points.size() * 20u;
    for (size_t i = 0; i < frame.points.size(); ++i) {
      const auto* bytes = cloud.data() + start + i * 20u;
      require(bytes[14] == frame.points[i].line && bytes[15] == 1u &&
                  readU32(bytes + 16) == frame.points[i].offset_time_ns,
              "ROS serializer lost line or changed point time");
    }
  }
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  try {
    for (const std::string name : {"livox_mid360", "livox_mid360s"}) {
      LidarFrameAccumulator acc;
      size_t next = 0, frames = 0;
      uint64_t time_us = 1800000000000000ULL;
      for (unsigned packet = 0; packet < 400; ++packet) {
        Bytes bytes;
        // 95-point fixture ensures a re-indexed line is detectably wrong.
        for (unsigned i = 0; i < 95; ++i) {
          appendU32(&bytes, packet * 95u + i); appendU32(&bytes, 2); appendU32(&bytes, 3);
          appendU8(&bytes, 77); appendU8(&bytes, 32);
          appendU8(&bytes, i % 4u); appendU8(&bytes, 1);
        }
        if (packet == 201) time_us += 100000; // an actual packet gap, not smeared
        for (const auto& frame : acc.append(time_us, 4700, name, bytes, 95)) {
          checkCloud(frame, next); ++frames;
        }
        time_us += 480;
      }
      if (const auto last = acc.finish()) { checkCloud(*last, next); ++frames; }
      require(frames >= 2 && next == 38000, "points lost during scan grouping");
    }

    // Optional integration with the actual RK Web writer; no device access.
    if (argc == 2) {
      QTemporaryDir temp;
      require(temp.isValid(), "temporary fixture directory failed");
      const QString jpeg = temp.path() + "/fixture.jpg";
      QImage image(16, 16, QImage::Format_RGB32); image.fill(Qt::black);
      require(image.save(jpeg, "JPEG"), "fixture JPEG failed");
      for (const QString epoch : {QStringLiteral("utc"), QStringLiteral("relative")}) {
        const QString dataset = temp.path() + "/" + epoch;
        QProcess writer;
        writer.start(QString::fromLocal8Bit(argv[1]),
                     {QStringLiteral("--viewer-fixture"), dataset, jpeg, epoch});
        require(writer.waitForFinished(30000) && writer.exitCode() == 0,
                writer.readAllStandardError().constData());
        const std::filesystem::path root = dataset.toStdString();
        require(validatePrismDataset(root).errorCount() == 0, "invalid Web dataset");
        DatasetPlaybackData data; std::string error;
        require(loadDatasetPlaybackData(root, {}, &data, &error), error.c_str());
        require(data.lidar_batches.size() == 3, "Web lidar batch count");
        for (const auto& batch : data.lidar_batches) {
          std::vector<DatasetPlaybackLidarPoint> points;
          require(loadDatasetLidarPoints(batch, &points, &error), error.c_str());
          require(points.size() == 2 && points[0].line_valid && points[0].line == 3 &&
                      points[1].line_valid && points[1].line == 2,
                  "Web recorded payload lost line metadata");
        }
        for (const auto format : {RosbagFormat::Ros1, RosbagFormat::Ros2}) {
          const auto output = root.parent_path() /
              (epoch.toStdString() + (format == RosbagFormat::Ros1 ? ".bag" : "-ros2"));
          const auto result = exportDatasetToRosbag(root, output, format, false);
          require(result.success, result.error.c_str());
          require(result.lidar_points == 6, "Web bag lost lidar points");
        }
      }
    }
    std::cout << "MID line: scan grouping, ROS1/ROS2 and Web writer/playback passed\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
