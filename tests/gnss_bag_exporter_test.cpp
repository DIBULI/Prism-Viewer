#include "dataset/gnss_bag_data.hpp"
#include "dataset/rosbag_exporter.hpp"
#include <QtCore/QCoreApplication>
#include <QtCore/QTemporaryDir>
#include <iostream>

namespace gb = prism_viewer::dataset::gnss_bag;
namespace ds = prism_viewer::dataset;
namespace fs = std::filesystem;
void require(bool test, const std::string& message) { if (!test) throw std::runtime_error(message); }
void write(const fs::path& path, const std::string& text) { std::ofstream f(path, std::ios::binary); f << text; require(bool(f), "fixture write"); }
const std::string position_header = "recording_elapsed_us,agent_session,sequence,agent_received_monotonic_ms,source,receiver_epoch,receiver_time_scale,valid,solution,quality,satellites_used,latitude_deg,longitude_deg,ellipsoid_height_m,hdop,north_sigma_m,east_sigma_m,up_sigma_m,solution_status,differential_age_s\n";
const std::string quality_header = "recording_elapsed_us,agent_session,sequence,agent_received_monotonic_ms,source,receiver_epoch,system_id,fix_dimension,pdop,hdop,vdop,rms_m,semi_major_sigma_m,semi_minor_sigma_m,ellipse_orientation_deg,north_sigma_m,east_sigma_m,up_sigma_m\n";
const std::string anchors = "phase,sensor_id,timestamp_us,sample_id,timestamp_synced,sample_gap,recording_elapsed_us\nrecording,0,1780000000000000,1,1,0,0\nrecording,0,1780000001000000,2,1,0,1000000\n";
void fixture(const fs::path& root) {
  write(root / "dataset.info", "format=prism-dataset-v5\n");
  write(root / "imu0.tum", "1780000000.0 0 0 9.8 0 0 0\n");
  write(root / "imu1.tum", "1780000000.0 0 0 9.8 0 0 0\n");
  // All camera topics plus seven GNSS topics exercise connection pointer stability.
  write(root / "camera.bin", std::string("\xff\xd8\xff\xd9", 4));
  for (int i=0; i<4; ++i) write(root / ("cam"+std::to_string(i)+".tum"), "1780000000.0 camera.bin 0 4 1000\n");
  write(root / "imu_metadata.csv", anchors);
  write(root / "gnss_receiver.csv", position_header +
    "100000,1,10,5100,GGA,123456.00,UTC-time-of-day,1,FIX,4,22,31,118,15,0.7,,,,,0.5\n"
    "200000,1,11,5200,GGA,123456.10,UTC-time-of-day,1,FLOAT,5,18,31,118,15,0.8,,,,,0.6\n"
    "300000,1,12,5300,GGA,123456.20,UTC-time-of-day,0,INVALID,0,0,,,,,,,,,\n"
    "400000,2,13,5400,GGA,123456.00,UTC-time-of-day,1,SINGLE,1,10,31,118,15,1.2,,,,,\n");
  write(root / "gnss_receiver_rtk.csv", position_header +
    "100000,1,20,5100,ADRNAV,2439:203054700,GPS-week:milliseconds,1,NARROW_INT,4,22,31,118,15,,0.02,0.01,0.03,SOL_COMPUTED,\n"
    "200000,1,21,5200,ADRNAV,2439:203054800,GPS-week:milliseconds,1,NARROW_FLOAT,5,18,31,118,15,,0,0.01,0.03,SOL_COMPUTED,\n"
    "300000,1,22,5300,ADRNAV,2439:203054900,GPS-week:milliseconds,0,NONE,0,0,,,,,,,,INSUFFICIENT_OBS,\n");
  write(root / "gnss_quality.csv", quality_header +
    "110000,1,30,5110,GNGST,123456.0,,,,,,1,0.02,0.01,20,0.2,0.1,0.3\n"
    "210000,1,31,5210,GNGST,123456.10,,,,,,1,0.02,0.01,20,0,0.1,0.3\n"
    "410000,1,32,5410,GNGST,123456.00,,,,,,1,0.02,0.01,20,9,9,9\n"
    "120000,1,33,5120,GNGSA,,1,3,1.2,0.7,1.0,,,,,,,\n");
}
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  try {
    // Headless, non-device regression/export entry point used for real recordings.
    if (argc == 4) {
      const auto result = ds::exportDatasetToRosbag(argv[1], argv[2],
        std::string(argv[3]) == "ros1" ? ds::RosbagFormat::Ros1 : ds::RosbagFormat::Ros2, false);
      require(result.success, result.error);
      std::cout << "gnss=" << result.gnss_positions << " rtk=" << result.rtk_positions << " quality=" << result.gnss_quality_messages << '\n';
      return 0;
    }
    QTemporaryDir temp; require(temp.isValid(), "temporary directory");
    const fs::path root = temp.path().toStdString(); fixture(root);
    const auto data = gb::load(root, []{});
    require(data.positions.size()==7 && data.quality.size()==4, "row accounting");
    const auto& p = data.positions[0];
    require(p.valid && p.covariance_valid && p.east==0.1 && p.north==0.2 && p.up==0.3, "GST association / ENU precision");
    require(p.stamp==1780000000100000ULL, "bag arrival timestamp");
    require(!data.positions[1].covariance_valid && !data.positions[2].valid && !data.positions[3].covariance_valid,
            "invalid sigma, invalid fix or cross-session GST accepted");
    require(data.positions[4].covariance_valid && !data.positions[5].covariance_valid && !data.positions[6].valid,
            "RTK native precision / invalid solutions");
    for (const auto format : {ds::RosbagFormat::Ros1, ds::RosbagFormat::Ros2}) {
      auto result = ds::exportDatasetToRosbag(root, root / (format == ds::RosbagFormat::Ros1 ? "fixture.bag" : "fixture.rosbag2"), format, false);
      require(result.success, result.error);
      require(result.gnss_positions==4 && result.rtk_positions==3 && result.gnss_quality_messages==4 && result.camera_messages==4,
              "export accounting");
    }
    if (argc == 2) {
      fs::path keep(argv[1]); require(!fs::exists(keep), "refusing to overwrite fixture destination");
      fs::copy(root, keep, fs::copy_options::recursive);
    }
    write(root / "imu_metadata.csv", "sensor_id,sample_id,timestamp_us,flags,phase,elapsed_us\n0,1,1780000000000000,136,recording,0\n0,2,1780000001000000,136,recording,1000000\n");
    require(gb::load(root, []{}).positions[0].stamp == p.stamp, "device-recorded IMU schema");
    write(root / "imu_metadata.csv", anchors);
    auto cancelled = ds::exportDatasetToRosbag(root, root / "cancelled.bag", ds::RosbagFormat::Ros1, false, {}, []{return true;});
    require(cancelled.cancelled && !fs::exists(root / "cancelled.bag"), "cancel cleanup");
    // All precision values must be positive, finite, and square without overflow.
    require(!gb::sigmaValid(-1,1,1) && !gb::sigmaValid(NAN,1,1) && !gb::sigmaValid(1,INFINITY,1) && !gb::sigmaValid(1e300,1,1), "sigma validation");
    require(gb::splitCsv("a,\"comma,quote\"\"ok\",,").size()==4, "CSV quoting");
    for (const auto bad : {"a,\"broken", "a,\"x\"y"}) {
      bool failed=false; try { gb::splitCsv(bad); } catch (...) { failed=true; }
      require(failed, "malformed CSV accepted");
    }
    write(root / "gnss_receiver_rtk.csv", position_header + "100000,1\n");
    auto failure = ds::exportDatasetToRosbag(root, root / "bad.bag", ds::RosbagFormat::Ros1, false);
    require(!failure.success && !fs::exists(root / "bad.bag"), "malformed row was silently skipped");
    fixture(root); write(root / "imu_metadata.csv", "phase,sensor_id,timestamp_us,sample_id,timestamp_synced,sample_gap,recording_elapsed_us\n");
    failure = ds::exportDatasetToRosbag(root, root / "bad.bag", ds::RosbagFormat::Ros1, false);
    require(!failure.success && failure.error.find("anchor")!=std::string::npos, "missing clock mapping was fabricated");
    fixture(root); write(root / "imu_metadata.csv", "phase,sensor_id,timestamp_us,sample_id,timestamp_synced,sample_gap,recording_elapsed_us\nrecording,0,1780000010000000,1,1,0,10000000\n");
    failure = ds::exportDatasetToRosbag(root, root / "bad.bag", ds::RosbagFormat::Ros1, false);
    require(!failure.success && failure.error.find("anchor")!=std::string::npos, "stale anchor accepted");
    std::cout << "GNSS / RTK bag exporter tests passed\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
