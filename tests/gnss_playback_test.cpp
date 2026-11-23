#include "dataset/dataset_playback.hpp"
#include "gnss_playback_fixture.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QProcess>
#include <iostream>
#include <stdexcept>
using namespace prism_viewer::dataset;
namespace fs=std::filesystem;
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
void write(const fs::path& p,const std::string& s){std::ofstream out(p);out<<s;check(bool(out),"fixture write");}
int main(int argc,char** argv) try {
  QCoreApplication app(argc,argv);QTemporaryDir temp;check(temp.isValid(),"temp");
  const auto root=fs::u8path(temp.path().toUtf8().constData());
  gnss_fixture::create(root);write(root/"dataset.info","format=prism-dataset-v5\ndevice_model=413GSV0\n");
  DatasetPlaybackData data;std::string error;
  const auto load=[&]{return loadDatasetPlaybackData(root,{},&data,&error);};
  const auto ui=[&]{
    if(argc!=2)return;
    QProcess process;process.start(QString::fromLocal8Bit(argv[1]),{"--gnss-playback-self-test",temp.path()});
    check(process.waitForFinished(30000),"GNSS UI timeout");
    std::cout<<process.readAllStandardOutput().toStdString()<<process.readAllStandardError().toStdString();
    check(process.exitStatus()==QProcess::NormalExit&&process.exitCode()==0,"GNSS UI failed");
  };
  check(load(),error.c_str());
  check(data.gnss_observations.size()==8&&data.timeline.empty()&&data.gnss_timeline.size()==8&&!data.empty(),"independent GNSS timeline");
  check(data.gnss_independent&&data.gnss_observations.back().gap,"cache gap was lost");
  check(data.gnss_observations[5].sentence==gnss_fixture::adr("NARROW_INT",200300),"original epoch or bytes changed");ui();
  const auto anchors=[&](bool web){
    std::string text=web?"phase,elapsed_us,timestamp_us\n":"phase,recording_elapsed_us,timestamp_us,timestamp_synced\n";
    for(unsigned i=0;i<8;++i)text+="recording,"+std::to_string(1000000+i*300000)+","+std::to_string(11000000+i*300000)+(web?"\n":",1\n");
    write(root/"imu_metadata.csv",text);
  };
  anchors(false);check(load(),error.c_str());
  check(!data.gnss_independent&&data.timeline.size()==8&&data.timeline.front().timestamp_us==11000000&&data.gnss_timeline.empty(),"Viewer clock anchor");ui();
  anchors(true);check(load(),error.c_str());check(!data.gnss_independent&&data.timeline.back().timestamp_us==13100000,"Web clock anchor");
  write(root/"imu_metadata.csv","host_elapsed_us,timestamp_us\n1000000,11000000\n");
  check(load(),error.c_str());check(data.gnss_independent,"RAW clock origin must not be inferred");
  write(root/"imu_metadata.csv","phase,recording_elapsed_us,timestamp_us,timestamp_synced\nrecording,1000000,11000000,0\n");
  check(load(),error.c_str());check(data.gnss_independent,"unsynced anchor");
  write(root/"imu_metadata.csv","phase,elapsed_us,timestamp_us\nrecording,1000000,11000000\n");
  check(load(),error.c_str());check(data.gnss_independent&&data.gnss_observations[0].timestamp_us==1000000,"partial mapping must not mix clocks");
  write(root/"imu_metadata.csv","phase,elapsed_us,timestamp_us\nrecording,1000000,11000000\nrecording,1300000,10000000\n");
  check(load(),error.c_str());check(data.gnss_independent,"regressed mapped clock");
  fs::remove(root/"imu_metadata.csv");
  const auto good=fs::file_size(root/"gnss_observations.bin");fs::resize_file(root/"gnss_observations.bin",good-1);
  check(!load()&&data.empty()&&error.find("range")!=std::string::npos,"truncated binary accepted");
  write(root/"gnss_observations.csv","recording_elapsed_us,agent_session,sequence,agent_received_monotonic_ms,byte_offset,byte_size,checksum_valid\n1,42,1,1,18446744073709551615,1,1\n");
  check(!load()&&error.find("range")!=std::string::npos,"overflow range accepted");
  fs::remove(root/"gnss_observations.csv");fs::remove(root/"gnss_observations.bin");
  check(load()&&data.empty(),"absent optional GNSS");
  std::cout<<"PASS GNSS recorded bytes, standalone/shared clocks, gap, UI playback/seek/reload, malformed range\n";
  return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
