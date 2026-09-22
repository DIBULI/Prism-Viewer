// Include the exporter implementation to test its private timing accumulator
// and ROS serializers without adding test-only public SDK functions.
#include "dataset/rosbag_exporter.cpp"
#include "dataset/dataset_playback.hpp"
#include "dataset/dataset_browser.hpp"
#include <QtCore/QCoreApplication>
#include <iostream>

using namespace prism_viewer::dataset;
static void require(bool ok, const char* what) {
  if (!ok) throw std::runtime_error(what);
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  try {
    Bytes bytes;
    const int32_t offsets[] = {-344000, 52872, -344000};
    for (unsigned i=0;i<3;++i) {
      appendU32(&bytes, 1000+i);appendU32(&bytes, 2000);appendU32(&bytes, 3000);
      appendU8(&bytes, 77);appendU8(&bytes, 0);
      appendU8(&bytes, uint8_t(i));appendU8(&bytes, 0);
      appendU32(&bytes, uint32_t(offsets[i]));
      appendU8(&bytes, uint8_t(i+1));appendU8(&bytes, 17);
      appendU8(&bytes, 0);appendU8(&bytes, 0);
    }
    LidarFrameAccumulator acc;
    require(acc.append(1000000,0,"hesai_xt32",bytes,3,true).empty(),"premature frame");
    const auto frames=acc.append(1100000,0,"hesai_xt32",bytes,3,true);
    require(frames.size()==1 && frames[0].points.size()==3,"dual returns were dropped");
    const auto& f=frames[0];
    require(f.timebase_ns==999656000 && f.explicit_point_time,"negative tail offset");
    require(f.points[0].offset_time_ns==0 && f.points[1].offset_time_ns==0 &&
            f.points[2].offset_time_ns==396872,"explicit times were interpolated");
    require(f.points[0].ring==0 && f.points[1].ring==2 && f.points[2].ring==1,
            "ring/dual-return association lost");
    for(const auto& ros : {makeRos1PointCloud2(0,f),makeRos2PointCloud2(f)}) {
      const size_t start=ros.size()-1-f.points.size()*24;
      for(size_t i=0;i<f.points.size();++i) {
        const auto* p=ros.data()+start+i*24;
        require(readU32(p+16)==f.points[i].offset_time_ns && p[14]==f.points[i].ring &&
                p[20]==f.points[i].return_id && p[21]==17,"ROS point metadata mismatch");
      }
    }
    try { LidarFrameAccumulator bad;bad.append(1,0,"hesai_xt32",bytes,3,true);
      throw std::logic_error("time underflow accepted");
    } catch(const std::overflow_error&) {}

    // Optional real Web-writer fixture: validate, replay and export both bags.
    if(argc==3) {
      const std::filesystem::path root=argv[1], dest=argv[2];
      const auto validation=validatePrismDataset(root);
      for (const auto& issue : validation.issues)
        std::cerr << issue.file << ':' << issue.line << ' ' << issue.message << '\n';
      require(validation.errorCount()==0,"Web XT32 dataset validation failed");
      DatasetPlaybackData data;std::string error;
      require(loadDatasetPlaybackData(root,{},&data,&error),error.c_str());
      require(data.lidar_batches.size()==3,"Web XT32 batch count");
      std::vector<DatasetPlaybackLidarPoint> points;
      require(loadDatasetLidarPoints(data.lidar_batches[0],&points,&error),error.c_str());
      require(points.size()==2 && points[0].offset_ns==-344000 && points[1].offset_ns==52872 &&
              points[1].ring==31 && points[0].return_id==1,"Web XT32 playback metadata");
      std::filesystem::create_directories(dest);
      for(const auto format : {RosbagFormat::Ros1,RosbagFormat::Ros2}) {
        const auto result=exportDatasetToRosbag(root,dest/(format==RosbagFormat::Ros1?"xt32.bag":"xt32-ros2"),format,false);
        require(result.success,result.error.c_str());
        require(result.lidar_points==6 && result.lidar_messages==3,"XT32 bag count");
      }
    }
    std::cout<<"XT32 signed time, dual return, ROS1/ROS2 and dataset tests passed\n";
    return 0;
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
