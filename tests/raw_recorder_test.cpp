#include "dataset/raw12_recorder.hpp"
#include "dataset/raw12_dataset.hpp"
#include "common/ui_text.hpp"
#include "raw_dataset_fixture.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <chrono>
#include <thread>
#include <atomic>
#include <iostream>
using namespace prism_viewer::dataset;
void check(bool b,const char* s){if(!b)throw std::runtime_error(s);}
Raw12RecordingStatus wait(Raw12Recorder& r){
  r.finish();const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(35);
  while(!r.status().done){check(std::chrono::steady_clock::now()<deadline,"finalization timeout");std::this_thread::sleep_for(std::chrono::milliseconds(10));}
  return r.status();
}
int main(int argc,char**argv){
  QCoreApplication app(argc,argv);
  try{
    QTemporaryDir temp;check(temp.isValid(),"temporary directory");
    auto parent=prism_viewer::common::toFilesystemPath(temp.path());
    auto prototype=raw_fixture::frame(0,0,1000000);
    auto image=[&](unsigned c,unsigned id){prism::capture::ProfileImage i;
      i.camera=uint8_t(c);i.format=4;i.width=5120;i.height=3072;i.encoded=prototype;
      auto put=[&](size_t at,uint32_t n){for(unsigned k=0;k<4;k++)i.encoded[at+k]=uint8_t(n>>(8*k));};
      i.encoded[37]=uint8_t(c);put(24,id);put(40,1000000+id*500000+c);return i;};
    Raw12RecordingOptions options;options.fps=2;
    // A late companion failure during full readback must not race a success
    // manifest. This reproduces a real Stop -> verification status race.
    {
      std::atomic<bool> entered{false},release{false};auto opt=options;
      opt.require_all_cameras=false;
      opt.before_verify=[&]{entered=true;const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(!release&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(1));};
      Raw12Recorder r(parent,opt);r.append(image(0,0));r.finish();
      const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
      while(!entered&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(1));
      const bool did_enter=entered;r.finish("late companion failure");release=true;
      const auto s=wait(r);check(did_enter&&!s.complete&&s.error=="late companion failure","Late finalization error marked complete");
      QFile f(prism_viewer::common::fromFilesystemPath(s.root/"dataset.info"));
      check(f.open(QIODevice::ReadOnly)&&f.readAll().contains("\ncomplete=0\n"),"Late failure success manifest");
    }
    // Receiving only one camera, or no cameras, must not invent a required
    // four-lane/IMU set. This is the production all-received policy.
    for(unsigned present=0;present<2;present++){
      auto opt=options;opt.record_any_imu=true;opt.require_all_cameras=false;
      Raw12Recorder r(parent,opt);if(present)r.append(image(2,0));
      const auto s=wait(r);check(s.complete&&s.frames[2]==present&&s.imu_samples[0]==0,
        "Absent camera/IMU blocked received-stream recording");
      check(validateRaw12Dataset(s.root,{},{}).valid,"Partial/no-camera dataset readback");
    }
    auto imu=[](uint32_t id,uint64_t stamp){prism::ImuSample s;
      s.sensor_id=0;s.format=2;s.flags=128;s.timestamp_synced=true;s.sample_id=id;s.timestamp_us=stamp;
      s.accel_mg={-1000,0,1000};s.gyro_mdps={-90000,0,180000};s.temp_milli_c=-12345;return s;};
    // The actual RAW12 writer receives every IMU callback even when the IMU
    // page is hidden. Verify original integer metadata, units and 16-bit wrap.
    {auto opt=options;opt.required_imu_mask=1;
      Raw12Recorder r(parent,opt);
      for(unsigned c=0;c<4;c++)r.append(image(c,0));
      for(unsigned i=0;i<400;i++){
        auto s=imu(uint16_t(65530+i),2000000+i*1250);
        s.fsync_event=i==0;s.fsync_delay_valid=i==0;r.appendImu(s);
      }
      const auto s=wait(r);check(s.complete&&s.imu_samples[0]==400&&s.imu_received[0]==400,"RAW12+IMU finalization/counts");
      QFile csv(prism_viewer::common::fromFilesystemPath(s.root/"imu_metadata.csv"));
      check(csv.open(QIODevice::ReadOnly),"metadata readback");const auto bytes=csv.readAll();
      check(bytes.count('\n')==401&&bytes.contains("0,2,128,65530,2000000,1,1,1,0,-1000,0,1000,-90000,0,180000,-12345,")&&
        bytes.contains("0,2,128,0,2007500,"),"IMU raw values/rollover changed");
      QFile tum(prism_viewer::common::fromFilesystemPath(s.root/"imu0.tum"));
      check(tum.open(QIODevice::ReadOnly),"TUM readback");const auto t=tum.readAll();
      check(t.count('\n')==401&&t.contains("2.000000 -9.806650000 0.000000000 9.806650000 -1.570796327 0.000000000 3.141592654\n"),"IMU timestamp or units changed");
      r.appendImu(imu(0,1));check(r.status().imu_received==s.imu_received,"IMU appended after STOP");
    }
    for(unsigned fault=0;fault<5;fault++){
      auto opt=options;opt.required_imu_mask=1;Raw12Recorder r(parent,opt);
      r.appendImu(imu(10,2000000));auto bad=imu(11,2001250);
      if(fault==0)bad.sample_id=10;
      if(fault==1)bad.sample_id=12;
      if(fault==2){bad.timestamp_us=1999999;bad.fsync_event=true;bad.fsync_delay_valid=true;}
      if(fault==3)bad.sample_gap=true;
      if(fault==4)bad.timestamp_synced=false;
      r.appendImu(bad);const auto s=wait(r);
      check(!s.complete&&s.error.find("IMU sequence/gap/time")!=std::string::npos&&s.imu_samples[0]==2,"Bad IMU lost or accepted");
    }
    {auto opt=options;opt.required_imu_mask=1;Raw12Recorder r(parent,opt);
      for(unsigned c=0;c<4;c++)r.append(image(c,0));
      const auto s=wait(r);check(!s.complete&&s.error.find("Required IMU")!=std::string::npos,"No-IMU recording marked complete");}
    {std::atomic<bool> blocked{true},entered{false};auto opt=options;
      opt.required_imu_mask=1;opt.imu_queue_samples=2;
      opt.before_write=[&]{entered=true;const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(blocked&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));};
      Raw12Recorder r(parent,opt);r.append(image(0,0));
      const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
      while(!entered&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));
      if(!entered){blocked=false;throw std::runtime_error("IMU queue test did not start");}
      for(unsigned i=0;i<3;i++)r.appendImu(imu(i,2000000+i*1250));
      blocked=false;const auto s=wait(r);
      check(!s.complete&&s.imu_received[0]==2&&s.imu_samples[0]==2&&s.error.find("IMU recording queue full")!=std::string::npos,
        "IMU overflow not bounded/fail-closed or evicted existing samples");
    }
    {Raw12Recorder r(parent,options);
      bool rejected=false;try{loadRaw12Dataset(r.status().root);}catch(...){rejected=true;}check(rejected,"active dataset replay permitted");
      for(unsigned id=0;id<3;id++)for(unsigned c=0;c<4;c++)r.append(image(c,id));
      const auto s=wait(r);check(s.complete&&s.error.empty()&&s.frames==std::array<uint64_t,4>{3,3,3,3},"2FPS original recording");
      const auto v=validateRaw12Dataset(s.root,{},{});check(v.valid&&v.checked_records==12&&v.cameras[0].median_interval_us==500000,"2FPS readback");
      r.append(image(0,99));check(r.status().frames==s.frames,"append after stop");
    }
    {std::atomic<bool> blocked{true},entered{false};options.queue_frames=1;
      options.before_write=[&]{entered=true;while(blocked)std::this_thread::sleep_for(std::chrono::milliseconds(1));};
      Raw12Recorder r(parent,options);r.append(image(0,0));
      const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
      while(!entered&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));
      if(!entered){blocked=false;throw std::runtime_error("writer did not start");}
      for(unsigned id=0;id<4;id++)for(unsigned c=0;c<4;c++)if(id||c)r.append(image(c,id));
      blocked=false;const auto s=wait(r);
      check(s.complete&&s.dropped==std::array<uint64_t,4>{3,4,4,3}&&
        s.frames==std::array<uint64_t,4>{1,0,0,1}&&s.received==std::array<uint64_t,4>{4,4,4,4},"oldest queue eviction counts");
      QFile manifest(prism_viewer::common::fromFilesystemPath(s.root/"dataset.info"));
      check(manifest.open(QIODevice::ReadOnly)&&manifest.readAll().contains("dropped_frames_total=14\n"),"persist total drops");
      check(validateRaw12Dataset(s.root,{},{}).valid,"queue drops not replayable");
    }
    // Repeated eviction carries the same-camera gap into its next queued item,
    // then into the tail if that successor is itself evicted.
    {std::atomic<bool> blocked{true},entered{false};options={};options.queue_frames=3;
      options.before_write=[&]{entered=true;const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(blocked){if(std::chrono::steady_clock::now()>end)throw std::runtime_error("stall expired");
          std::this_thread::sleep_for(std::chrono::milliseconds(1));}};
      Raw12Recorder r(parent,options);r.append(image(0,0));
      const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
      while(!entered&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(1));
      if(!entered){blocked=false;throw std::runtime_error("writer startup timeout");}
      r.append(image(0,1));r.append(image(0,2));r.append(image(1,0));
      r.append(image(2,0));r.append(image(3,0));r.append(image(0,3));
      blocked=false;const auto s=wait(r);
      check(s.complete&&s.frames==std::array<uint64_t,4>{2,0,1,1}&&
        s.dropped==std::array<uint64_t,4>{2,1,0,0},"same-camera gap transfer");
      check(validateRaw12Dataset(s.root,{},{}).valid,"chained eviction readback");
      QFile info(prism_viewer::common::fromFilesystemPath(s.root/"dataset.info"));
      check(info.open(QIODevice::ReadOnly),"read manifest");auto text=info.readAll();info.close();
      text.replace("dropped_frames_total=3\n","dropped_frames_total=2\n");
      check(info.open(QIODevice::WriteOnly|QIODevice::Truncate)&&info.write(text)==text.size(),"tamper total fixture");info.close();
      bool rejected=false;try{loadRaw12Dataset(s.root);}catch(...){rejected=true;}
      check(rejected,"inconsistent aggregate drops accepted");
    }
    {std::atomic<bool> blocked{true},entered{false};options={};
      auto expanded=[&](unsigned c,unsigned id,unsigned mode){auto i=image(c,id);auto& b=i.encoded;
        const auto block=5+((256-2)*mode+7)/8;const auto seed=std::vector<uint8_t>(b.begin()+65,b.begin()+69);
        b.resize(64);b.resize(64+61440*block);
        for(size_t at=64;at<b.size();at+=block){b[at]=uint8_t(mode);std::copy(seed.begin(),seed.end(),b.begin()+at+1);}
        auto put=[&](size_t at,uint32_t n){for(unsigned k=0;k<4;++k)b[at+k]=uint8_t(n>>(8*k));};
        put(56,b.size()-64);put(48,prism::capture::rawCrc32(b.data()+64,b.size()-64));return i;};
      options.queue_bytes=prototype.size()+expanded(0,0,1).encoded.size();
      options.before_write=[&]{entered=true;const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(blocked){if(std::chrono::steady_clock::now()>end)throw std::runtime_error("byte stall expired");
          std::this_thread::sleep_for(std::chrono::milliseconds(1));}};
      Raw12Recorder r(parent,options);r.append(image(0,0));
      const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
      while(!entered&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(1));
      if(!entered){blocked=false;throw std::runtime_error("byte writer startup timeout");}
      r.append(image(0,1));r.append(image(1,0));r.append(image(2,0));r.append(image(3,0));
      r.append(expanded(0,2,1));r.append(expanded(2,1,2));
      const auto queued=r.status().queued_bytes;blocked=false;const auto s=wait(r);
      check(s.complete&&queued==options.queue_bytes&&s.frames==std::array<uint64_t,4>{2,0,0,1}&&
        s.dropped==std::array<uint64_t,4>{1,1,2,0},"multi-eviction / oversized incoming retention");
      check(validateRaw12Dataset(s.root,{},{}).valid,"byte pressure readback");
    }
    options={};options.fps=2;
    {Raw12Recorder r(parent,options);r.append(image(0,0));r.append(image(0,2));const auto s=wait(r);
      check(!s.complete&&!s.error.empty(),"source gap accepted");}
    {Raw12Recorder r(parent,options);for(unsigned c=0;c<4;c++){
        auto i=image(c,0);if(c==1)i.encoded[20]^=1;r.append(i);}
      const auto s=wait(r);check(!s.complete&&!s.error.empty(),"bad decoded CRC accepted");}
    {options.before_write=[](){throw std::runtime_error("injected disk failure");};
      Raw12Recorder r(parent,options);r.append(image(0,0));const auto s=wait(r);
      check(!s.complete&&s.error=="injected disk failure","write error hidden");}
    {options={};options.fps=4;bool rejected=false;try{Raw12Recorder r(parent,options);}catch(...){rejected=true;}
      check(rejected,"rate above2FPS accepted");}
    std::cout<<"PASS RAW12 writer:2FPS fixture, no overwrite, full readback CRC, bounded queue drops, gaps and I/O failures\n";
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
