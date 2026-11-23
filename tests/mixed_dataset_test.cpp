#include "dataset/raw8_dataset.hpp"
#include "dataset/raw8_recorder.hpp"
#include "common/ui_text.hpp"
#include "ui/bayer_preview.hpp"
#include "raw_dataset_fixture.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QBuffer>
#include <QProcess>
#include <QColor>
#include <iostream>
#include <chrono>
#include <thread>
using namespace prism_viewer::dataset;
namespace fs=std::filesystem;
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
namespace {
std::vector<uint8_t> frame(unsigned c,uint32_t id,uint64_t stamp,unsigned mode) {
  using namespace native8_fixture;
  if(c==1||c==2) {
    constexpr unsigned pixels=4096*3072;
    static const auto crc=[] {std::vector<uint8_t> p(pixels,0x83);return prism::capture::rawCrc32(p.data(),p.size());}();
    auto b=header(c,id,stamp,crc);put(b,8,4096);put(b,16,pixels);put(b,64,pixels/256);
    put(b,68,3);put(b,72,6000);b[38]=uint8_t(mode);put(b,52,mode==4?id:0);
    // Old zero-valued metadata remains visible as recorded.
    if(c==2){put(b,28,0);put(b,32,0);}
    for(unsigned n=0;n<pixels/256;++n)b.insert(b.end(),{0,0x83,0x83});
    seal(b);return b;
  }
  QImage image(1280,1024,QImage::Format_RGB32);image.fill(QColor(40,90,120));
  QByteArray jpeg;QBuffer buffer(&jpeg);buffer.open(QIODevice::WriteOnly);
  check(image.save(&buffer,"JPEG"),"JPEG fixture");
  std::vector<uint8_t> h(64);std::memcpy(h.data(),"PRJ1",4);
  h[4]=2;h[5]=64;h[28]=uint8_t(c);h[29]=1;h[30]=uint8_t(mode);
  put(h,8,1280);put(h,12,1024);put(h,16,id);put(h,20,1200);put(h,24,2048);
  put(h,32,uint32_t(stamp));put(h,36,uint32_t(stamp>>32));
  put(h,40,prism::capture::rawCrc32(reinterpret_cast<const uint8_t*>(jpeg.data()),jpeg.size()));
  put(h,44,uint32_t(jpeg.size()));put(h,48,mode==4?id:0);put(h,60,prism::capture::rawCrc32(h.data(),60));
  std::vector<uint8_t> b={255,216,255,239,0,66};b.insert(b.end(),h.begin(),h.end());
  b.insert(b.end(),jpeg.begin()+2,jpeg.end());return b;
}
std::string fixture(const fs::path& root,bool drops=false,bool empty=false,bool unknown=false,unsigned jpeg_fps=12) {
  fs::create_directories(root);
  std::string info="format=prism-mixed-dataset-v1\ndevice_model=21312V0\ndevice_serial=fixture\ncodec=per-camera-jpeg-and-raw8-rice\ncomplete=1\ncamera_count=4\nframe_stride=1\ndrop_accounting=1\nhas_drops="+
      std::to_string(drops?1:0)+"\nframes_complete="+std::to_string(drops?0:1)+"\n";
  uint64_t total=0;
  for(unsigned c=0;c<4;++c) {
    const bool raw=c==1||c==2;const auto p="camera"+std::to_string(c);
    const unsigned received=empty&&c==3?0:raw?3:jpeg_fps+1;
    const unsigned dropped=drops&&c==1?1:0;
    std::string stream,index;uint64_t offset=0;unsigned pending=0,count=0;
    for(unsigned i=0;i<received;++i) {
      if(dropped&&i==1){++pending;continue;}
      const uint32_t id=10+i*(raw?3:1);
      const unsigned mode=unknown&&c==2&&i==0?0:4;
      const uint64_t stamp=mode?1000000ULL+(raw?250000+i*500000ULL:i*1000000ULL/jpeg_fps):0;
      auto b=frame(c,id,stamp,mode);
      QJsonObject row{{"offset",double(offset)},{"bytes",int(b.size())},{"frame_id",double(id)},
        {"timestamp_us",double(stamp)},{"timing_mode",int(mode)},{"trigger_sequence",mode==4?int(id):0},
        {"raw_crc32",double(prism::capture::rawLe32(b.data()+(raw?20:46)))},
        {"encoded_crc32",double(raw?prism::capture::rawLe32(b.data()+48):prism::capture::rawCrc32(b.data(),b.size()))},
        {"dropped_before",int(pending)}};
      if(pending)row["drop_reason"]="disk_queue_full";
      index+=QJsonDocument(row).toJson(QJsonDocument::Compact).toStdString()+"\n";
      stream.append(reinterpret_cast<const char*>(b.data()),b.size());offset+=b.size();pending=0;++count;
    }
    raw_fixture::save(root/(p+".jsonl"),index);
    raw_fixture::save(root/(p+(raw?".pr8-stream":".mjpg-stream")),stream);
    total+=offset;
    info+=p+"_width="+std::to_string(raw?4096:1280)+"\n"+p+"_height="+std::to_string(raw?3072:1024)+
      "\n"+p+"_source_bits=8\n"+p+"_profile="+std::to_string(raw?2:1)+
      "\n"+p+"_fps_per_camera="+std::to_string(raw?2:jpeg_fps)+"\n"+p+"_source_steps="+std::to_string(raw?3:1)+
      "\n"+p+"_images="+std::to_string(count)+"\n"+p+"_received="+std::to_string(received)+
      "\n"+p+"_dropped="+std::to_string(dropped)+"\n"+p+"_dropped_tail=0\n"+
      p+"_first_received_frame="+std::to_string(received?10:0)+"\n"+p+"_last_received_frame="+
      std::to_string(received?10+(received-1)*(raw?3:1):0)+"\n";
  }
  info+="bytes="+std::to_string(total)+"\n";raw_fixture::save(root/"dataset.info",info);return info;
}
void companionFixture(const fs::path& root) {
  for(unsigned sensor=0;sensor<2;++sensor)
    raw_fixture::save(root/("imu"+std::to_string(sensor)+".tum"),
        "1.100000 0.1 -0.2 9.8 0.01 -0.02 0.03\n2.400000 0.2 -0.3 9.7 0.02 -0.03 0.04\n");
  std::vector<uint8_t> points(32);
  native8_fixture::put(points,0,1000);native8_fixture::put(points,4,2000);native8_fixture::put(points,8,3000);
  native8_fixture::put(points,16,4000);native8_fixture::put(points,20,5000);native8_fixture::put(points,24,6000);
  raw_fixture::save(root/"lidar-data-0000.bin",std::string(reinterpret_cast<const char*>(points.data()),points.size()));
  raw_fixture::save(root/"lidar.tum",
      "1.100000 lidar-data-0000.bin 0 16 1 2 35 1 1 1100000000 100 1 0\n"
      "2.400000 lidar-data-0000.bin 16 16 1 2 35 1 2 2400000000 100 1 0\n");
  raw_fixture::save(root/"lidar_imu.tum",
      "1.100000 0.1 -0.2 9.8 0.01 -0.02 0.03 2 35 1 1 1100000000 1 0\n"
      "2.400000 0.2 -0.3 9.7 0.02 -0.03 0.04 2 35 1 2 2400000000 1 0\n");
}
}
int main(int argc,char** argv) {
  QCoreApplication app(argc,argv);
  try {
    QTemporaryDir temp;check(temp.isValid(),"temp");
    auto root=prism_viewer::common::toFilesystemPath(temp.path());
    auto info=fixture(root);
    if(argc==3&&std::string(argv[1])=="--record-ui") {
      for(unsigned fps:{12u,24u,36u}){
        fixture(root,false,false,false,fps);
        QProcess process;process.start(QString::fromLocal8Bit(argv[2]),{"--mixed-recording-self-test",temp.path(),QString::number(fps)});
        check(process.waitForFinished(60000),"mixed recording UI timeout");
        std::cout<<process.readAllStandardOutput().toStdString()<<process.readAllStandardError().toStdString();
        check(process.exitStatus()==QProcess::NormalExit&&process.exitCode()==0,"mixed recording UI failed");
      }
      return 0;
    }
    if(argc==3&&std::string(argv[1])=="--ui") {
      QProcess process;
      const auto playback=[&](bool success=true){
        process.start(QString::fromLocal8Bit(argv[2]),{"--dataset-playback-self-test",temp.path()});
        check(process.waitForFinished(80000),"mixed UI timeout");
        const auto output=process.readAllStandardOutput()+process.readAllStandardError();
        std::cout<<output.toStdString();
        check(process.exitStatus()==QProcess::NormalExit&&((process.exitCode()==0)==success),"mixed UI failed");
        if(!success)check(output.contains("imu0.tum"),"missing sensor parse error");
      };
      playback(); // Older camera-only recordings remain browsable.
      companionFixture(root);playback(); // Includes sensor samples after the last image.
      fixture(root,false,false,true);playback(); // Unknown image clocks do not hide sensors or invent alignment.
      fixture(root);raw_fixture::save(root/"imu0.tum","broken sample\n");playback(false);
      companionFixture(root);
      process.start(QString::fromLocal8Bit(argv[2]),{"--mixed-live-metadata-self-test",temp.path()});
      check(process.waitForFinished(15000),"mixed live metadata timeout");
      std::cout<<process.readAllStandardOutput().toStdString()<<process.readAllStandardError().toStdString();
      check(process.exitStatus()==QProcess::NormalExit&&process.exitCode()==0,"mixed live metadata failed");
      return 0;
    }
    auto data=loadRaw8Dataset(root);
    check(data.mixed&&!data.ordinal_only&&data.cameras[0].size()==13&&data.cameras[1].size()==3,"mixed index");
    check(data.display.front()==std::array<int,4>{0,-1,-1,0},"future RAW frame shown");
    const auto at=std::lower_bound(data.timestamps.begin(),data.timestamps.end(),1333333ULL)-data.timestamps.begin();
    check(data.display.at(at)==std::array<int,4>{4,0,0,4},"independent-rate latest sample");
    check(data.display.back()==std::array<int,4>{-1,2,2,-1},"tail RAW omitted or stale JPEG frozen");
    for(unsigned c=0;c<4;++c) {
      const auto& e=data.cameras[c][0];auto image=loadProfileDatasetImage(e);
      check(image.size()==(e.raw8?QSize(4096,3072):QSize(1280,1024)),"mixed dimensions");
      check(prism_viewer::ui::isNativeBayerPreview(image)==e.raw8,"mixed Bayer provenance");
      if(e.raw8){check(image.constScanLine(0)[0]==0x83,"RAW native pixels");
        check(prism_viewer::ui::raw8ThumbnailSource(image).size()==QSize(2048,1536),"12M CFA thumbnail");}
      else check(qBlue(image.pixel(0,0))>qRed(image.pixel(0,0)),"JPEG color lost");
    }
    check(data.cameras[0][0].exposure_us==1200&&data.cameras[0][0].gain_x1024==2048&&
          data.cameras[2][0].exposure_us==0&&data.cameras[2][0].gain_x1024==0,"metadata altered");
    auto valid=validateRaw8Dataset(root,{},{});
    check(valid.valid&&valid.checked_records==32&&valid.device_model=="21312V0","mixed validation");
    check(validateRaw8Dataset(root,{},[]{return true;}).cancelled,"mixed cancel");
    auto rejects=[&]{try{loadRaw8Dataset(root);return false;}catch(const std::exception&){return true;}};
    for(const auto& change:std::vector<std::pair<std::string,std::string>>{
        {"camera1_source_steps=3","camera1_source_steps=1"},{"camera0_width=1280","camera0_width=4096"},
        {"camera2_last_received_frame=16","camera2_last_received_frame=17"}}) {
      auto bad=info;bad.replace(bad.find(change.first),change.first.size(),change.second);
      raw_fixture::save(root/"dataset.info",bad);check(rejects(),"invalid manifest accepted");
    }
    raw_fixture::save(root/"dataset.info",info);
    {std::fstream f(root/"camera0.mjpg-stream",std::ios::binary|std::ios::in|std::ios::out);
      f.seekp(26);f.put(0);}
    check(rejects(),"JPEG header corruption accepted");
    fixture(root);
    {std::fstream f(root/"camera0.mjpg-stream",std::ios::binary|std::ios::in|std::ios::out);
      f.seekp(std::streamoff(data.cameras[0][0].byte_size-1));f.put(0);}
    check(!validateRaw8Dataset(root,{},{}).valid,"truncated JPEG accepted");
    fixture(root,true,true);data=loadRaw8Dataset(root);
    check(data.dropped[1]==1&&data.cameras[3].empty()&&data.cameras[1].size()==2,"drops/absent lane");
    check(validateRaw8Dataset(root,{},{}).valid,"accounted loss rejected");
    fixture(root,false,false,true);data=loadRaw8Dataset(root);
    check(data.ordinal_only&&data.display.size()==13&&data.cameras[2][0].timestamp_us==0,"unknown timestamp fabricated");
    check(validateRaw8Dataset(root,{},{}).valid,"unknown time image browsing rejected");
    // Validate product identity and metadata; do not apply the TF writer's
    // rate limit to the host's all-received recording path.
    Raw8RecordingOptions options;options.device_model="21312V0";options.device_serial="test";options.fps=12;
    for(unsigned fault=0;fault<4;++fault){
      auto bad=options;
      if(fault==0)bad.fps=0;if(fault==1)bad.fps=121;
      if(fault==2)bad.device_serial.clear();if(fault==3)bad.device_model="413GSV0";
      bool rejected=false;try{Raw8Recorder recorder(root,bad);}catch(const std::exception&){rejected=true;}
      check(rejected,"invalid mixed recording setup accepted");
    }
    for(unsigned fault=0;fault<3;++fault){
      Raw8Recorder recorder(root,options);prism::capture::ProfileImage image;
      image.camera=1;image.format=prism::capture::kRaw8Rice;image.width=4096;image.height=3072;
      image.timestamp_us=1000000;image.encoded=frame(1,10,1000000,4);
      if(fault==0)image.camera=0;
      if(fault==1)image.width=5120;
      if(fault==2){native8_fixture::put(image.encoded,68,1);native8_fixture::seal(image.encoded);}
      recorder.append(image);recorder.finish();
      const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
      while(!recorder.status().done&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(1));
      check(recorder.status().done&&!recorder.status().complete&&recorder.status().bytes==0,"invalid mixed frame persisted");
    }
    std::cout<<"PASS mixed JPEG/RAW8 independent timeline, native pixels, metadata, loss, absent lane, unknown clocks, malformed input\n";
  } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}

