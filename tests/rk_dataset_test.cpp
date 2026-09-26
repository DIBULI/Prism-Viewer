#include "dataset/rk_dataset_client.hpp"
#include "dataset/dataset_browser.hpp"
#include "dataset/rosbag_exporter.hpp"
#include <QtCore/QCoreApplication>
#include <QtCore/QJsonObject>
#include <QtCore/QProcess>
#include <QtCore/QTemporaryDir>
#include <QtGui/QImage>
#include <filesystem>
#include <iostream>
using namespace prism_viewer::dataset;
void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
template<class F> void rejects(F f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}require(caught,"expected failure");}
std::filesystem::path path(const QString& s){return std::filesystem::u8path(s.toUtf8().constData());}
void fixture(const QString& worker,const QString& root,const QString& epoch) {
  QImage image(16,16,QImage::Format_RGB32);image.fill(Qt::green);
  const auto jpeg=root+"/fixture.jpg";require(image.save(jpeg,"JPG"),"jpeg");
  const auto dataset=root+"/capture-"+epoch;
  QProcess process;process.start(worker,{"--viewer-fixture",dataset,jpeg,epoch});
  require(process.waitForFinished(30000)&&process.exitCode()==0,"recorder fixture");
  const auto valid=validatePrismDataset(path(dataset));require(valid.valid,"native raw dataset");
  require(valid.cameras[0].rows==3&&valid.onboard_imus[0].rows==3&&valid.lidar.rows==3&&valid.lidar_imu.rows==3,"raw counts");
  for(auto format:{RosbagFormat::Ros1,RosbagFormat::Ros2}) {
    const auto target=dataset+(format==RosbagFormat::Ros1?".bag":".rosbag2");
    const auto result=exportDatasetToRosbag(path(dataset),path(target),format,false);
    require(result.success&&result.camera_messages==12&&result.camera_exposure_messages==12&&
      result.imu_messages==3&&result.lidar_points==6&&result.lidar_imu_messages==3,"Viewer bag conversion");
  }
}
int main(int argc,char** argv) {
  QCoreApplication app(argc,argv);
  try {
    unsigned calls=0;RkDatasetAccess access;
    access.list=[&]{++calls;return std::vector<prism::RecordedDataset>{{"capture-test",123,true,true}};};
    const auto rows=listRkDatasets(access);require(calls==1&&rows.size()==1,"USB list callback");
    require(rows[0].toObject()["name"]=="capture-test","name");
    rejects([&]{listRkDatasets(access,[]{return true;});});require(calls==1,"cancel before USB");
    rejects([]{listRkDatasets({});});
    unsigned reports=0;
    access.download=[&](const std::string& n,const std::string& p,const prism::DatasetProgress& progress,const prism::DatasetCancel& cancel){
      require(n=="capture-test"&&p=="/raw"&&cancel&&!cancel(),"raw SDK parameters");
      progress({10,10,"events.csv"});return std::string("/raw/capture-test");
    };
    auto saved=downloadRkDataset(access,"capture-test","/raw",[&](quint64 n,quint64 total,const QString& f){
      require(n==10&&total==10&&f=="events.csv","progress");++reports;
    },[]{return false;});
    require(saved=="/raw/capture-test"&&reports==1,"SDK output path");
    access.download=[](const auto&,const auto&,const auto&,const auto&)->std::string{throw RkDownloadCancelled();};
    rejects([&]{downloadRkDataset(access,"capture-test","/raw");});
    const auto worker=qEnvironmentVariable("PRISM_WEB_WORKER");
    if(!worker.isEmpty()) {QTemporaryDir tmp;fixture(worker,tmp.path(),"boot");fixture(worker,tmp.path(),"utc");}
    std::cout<<"PASS USB raw SDK adapter; ROS conversion remains Viewer-only\n";return 0;
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
