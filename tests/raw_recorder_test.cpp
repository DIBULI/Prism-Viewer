#include "dataset/raw8_recorder.hpp"
#include "dataset/raw8_dataset.hpp"
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
Raw8RecordingStatus wait(Raw8Recorder& r){
  r.finish();const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(35);
  while(!r.status().done){check(std::chrono::steady_clock::now()<deadline,"finalization timeout");std::this_thread::sleep_for(std::chrono::milliseconds(10));}
  return r.status();
}
int main(int argc,char**argv){
  QCoreApplication app(argc,argv);
  try{
    QTemporaryDir temp;check(temp.isValid(),"temporary directory");
    auto parent=prism_viewer::common::toFilesystemPath(temp.path());
    auto prototype=raw_fixture::frame(0,1,1000000);
    uint32_t next_transfer=1001;
    auto image=[&](unsigned c,unsigned id){prism::capture::ProfileImage i;
      i.camera=uint8_t(c);i.format=prism::capture::kRaw8Rice;i.width=5120;i.height=3072;i.encoded=prototype;
      i.frame_id=next_transfer++;i.timestamp_us=1000000+id*500000+c;
      auto put=[&](size_t at,uint32_t n){for(unsigned k=0;k<4;k++)i.encoded[at+k]=uint8_t(n>>(8*k));};
      i.encoded[37]=uint8_t(c);put(24,id+1);put(40,uint32_t(i.timestamp_us));native8_fixture::sealHeader(i.encoded);return i;};
    Raw8RecordingOptions options;options.fps=2;
    // Actual v2 recording/replay retains shared group ID AND distinct native
    // counters. The preview barrier must not rewrite or drop recorded frames.
    {
      Raw8Recorder r(parent,options);
      for(unsigned c=0;c<4;++c) {
        auto i=image(c,10+c*100);
        auto& b=i.encoded;b[38]=4;i.timestamp_us=9000000;
        const auto put=[&](size_t at,uint32_t v){for(unsigned n=0;n<4;++n)b[at+n]=uint8_t(v>>(8*n));};
        put(40,9000000);put(44,0);put(52,91);native8_fixture::sealHeader(b);
        r.append(i);
      }
      const auto status=wait(r);
      check(status.complete&&status.frames==std::array<uint64_t,4>{1,1,1,1},"v2 group recording");
      const auto dataset=loadRaw8Dataset(status.root);
      check(dataset.timestamps.size()==1&&dataset.display[0]==std::array<int,4>{0,0,0,0},"v2 common playback");
      for(unsigned c=0;c<4;++c)
        check(dataset.cameras[c][0].trigger_sequence==91&&dataset.cameras[c][0].frame_id==11+c*100,
              "v2 group/source lost in index");
      check(validateRaw8Dataset(status.root,{},{}).valid,"v2 complete pixel CRC readback");
      // Tampering only a transport ID must fail; source/group/pixels still
      // have valid CRCs and cannot hide this independent delivery gap.
      QFile index(prism_viewer::common::fromFilesystemPath(status.root/"camera1.jsonl"));
      check(index.open(QIODevice::ReadOnly),"read transfer index");
      auto row=QJsonDocument::fromJson(index.readAll()).object();index.close();
      check(row.value("transfer_id").toInt()==1002,"transport ID was rewritten as source ID");
      row.insert("transfer_id",1001);
      check(index.open(QIODevice::WriteOnly|QIODevice::Truncate),"write tampered fixture");
      index.write(QJsonDocument(row).toJson(QJsonDocument::Compact)+"\n");index.close();
      check(!validateRaw8Dataset(status.root,{},{}).valid,"duplicate transport index accepted");
    }
    // A late companion failure during full readback must not race a success
    // manifest. This reproduces a real Stop -> verification status race.
    {
      std::atomic<bool> entered{false},release{false};auto opt=options;
      opt.require_all_cameras=false;
      opt.before_verify=[&]{entered=true;const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(!release&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(1));};
      Raw8Recorder r(parent,opt);r.append(image(0,0));r.finish();
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
      Raw8Recorder r(parent,opt);if(present)r.append(image(2,0));
      const auto s=wait(r);check(s.complete&&s.frames[2]==present&&s.imu_samples[0]==0,
        "Absent camera/IMU blocked received-stream recording");
      check(validateRaw8Dataset(s.root,{},{}).valid,"Partial/no-camera dataset readback");
    }
    auto imu=[](uint32_t id,uint64_t stamp){prism::ImuSample s;
      s.sensor_id=0;s.format=2;s.flags=128;s.timestamp_synced=true;s.sample_id=id;s.timestamp_us=stamp;
      s.accel_mg={-1000,0,1000};s.gyro_mdps={-90000,0,180000};s.temp_milli_c=-12345;return s;};
    // The actual RAW8 writer receives every IMU callback even when the IMU
    // page is hidden. Verify original integer metadata, units and 16-bit wrap.
    {auto opt=options;opt.required_imu_mask=1;
      Raw8Recorder r(parent,opt);
      for(unsigned c=0;c<4;c++)r.append(image(c,0));
      for(unsigned i=0;i<400;i++){
        auto s=imu(uint16_t(65530+i),2000000+i*1250);
        s.fsync_event=i==0;s.fsync_delay_valid=i==0;r.appendImu(s);
      }
      const auto s=wait(r);check(s.complete&&s.imu_samples[0]==400&&s.imu_received[0]==400,"RAW8+IMU finalization/counts");
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
      auto opt=options;opt.required_imu_mask=1;Raw8Recorder r(parent,opt);
      r.appendImu(imu(10,2000000));auto bad=imu(11,2001250);
      if(fault==0)bad.sample_id=10;
      if(fault==1)bad.sample_id=12;
      if(fault==2){bad.timestamp_us=1999999;bad.fsync_event=true;bad.fsync_delay_valid=true;}
      if(fault==3)bad.sample_gap=true;
      if(fault==4)bad.timestamp_synced=false;
      r.appendImu(bad);const auto s=wait(r);
      check(!s.complete&&s.error.find("IMU sequence/gap/time")!=std::string::npos&&s.imu_samples[0]==2,"Bad IMU lost or accepted");
    }
    {auto opt=options;opt.required_imu_mask=1;Raw8Recorder r(parent,opt);
      for(unsigned c=0;c<4;c++)r.append(image(c,0));
      const auto s=wait(r);check(!s.complete&&s.error.find("Required IMU")!=std::string::npos,"No-IMU recording marked complete");}
    {std::atomic<bool> blocked{true},entered{false};auto opt=options;
      opt.required_imu_mask=1;opt.imu_queue_samples=2;
      opt.before_write=[&]{entered=true;const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(blocked&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));};
      Raw8Recorder r(parent,opt);r.append(image(0,0));
      const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
      while(!entered&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));
      if(!entered){blocked=false;throw std::runtime_error("IMU queue test did not start");}
      for(unsigned i=0;i<3;i++)r.appendImu(imu(i,2000000+i*1250));
      blocked=false;const auto s=wait(r);
      check(!s.complete&&s.imu_received[0]==2&&s.imu_samples[0]==2&&s.error.find("IMU recording queue full")!=std::string::npos,
        "IMU overflow not bounded/fail-closed or evicted existing samples");
    }
    {Raw8Recorder r(parent,options);
      bool rejected=false;try{loadRaw8Dataset(r.status().root);}catch(...){rejected=true;}check(rejected,"active dataset replay permitted");
      for(unsigned id=0;id<3;id++)for(unsigned c=0;c<4;c++)r.append(image(c,id));
      const auto s=wait(r);check(s.complete&&s.error.empty()&&s.frames==std::array<uint64_t,4>{3,3,3,3},"2FPS original recording");
      const auto v=validateRaw8Dataset(s.root,{},{});check(v.valid&&v.checked_records==12&&v.cameras[0].median_interval_us==500000,"2FPS readback");
      r.append(image(0,99));check(r.status().frames==s.frames,"append after stop");
    }
    // Frame and byte bounds both fail without evicting queued/in-flight data.
    for(bool byte_limit:{false,true}){
      std::atomic<bool> blocked{true},entered{false};auto opt=options;opt.require_all_cameras=false;
      if(byte_limit)opt.queue_bytes=prototype.size();else opt.queue_frames=1;
      opt.before_write=[&]{entered=true;const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(blocked&&std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(std::chrono::milliseconds(1));};
      Raw8Recorder r(parent,opt);r.append(image(0,0));
      const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
      while(!entered&&std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(std::chrono::milliseconds(1));
      if(!entered){blocked=false;throw std::runtime_error("writer did not start");}
      r.append(image(0,1));r.append(image(1,0));
      const auto queued=r.status();blocked=false;const auto s=wait(r);
      check(queued.closing&&queued.queued_bytes==prototype.size()&&!s.complete&&
        s.frames==std::array<uint64_t,4>{2,0,0,0}&&s.dropped==std::array<uint64_t,4>{0,1,0,0}&&
        s.error.find("without eviction")!=std::string::npos,"overflow evicted frames or marked complete");
      check(!validateRaw8Dataset(s.root,{},{}).valid,"failed recording replayable as complete");
    }
    {auto opt=options;opt.queue_bytes=prototype.size()-1;Raw8Recorder r(parent,opt);
      r.append(image(0,0));const auto s=wait(r);
      check(!s.complete&&s.bytes==0&&s.error.find("queue full")!=std::string::npos,"oversized frame accepted");}
    // Transport metadata cannot disagree with the exact persisted envelope.
    for(unsigned field=0;field<5;++field){Raw8Recorder r(parent,options);auto i=image(0,0);
      if(field==0)++i.camera;if(field==1)++i.timestamp_us;if(field==2)++i.width;
      if(field==3)i.format=4;if(field==4)i.encoded[81]^=1;
      r.append(i);const auto s=wait(r);check(!s.complete&&s.bytes==0&&!s.error.empty(),"immutable metadata mismatch accepted");
    }
    options={};options.fps=2;
    // Global transport gaps/replays fail even if the native camera sequence
    // and full pixels are valid. Wrap is legal and retained exactly on disk.
    for(int delta:{-1,1}){auto opt=options;opt.require_all_cameras=false;Raw8Recorder r(parent,opt);
      auto a=image(2,10),b=image(2,11);b.frame_id=uint32_t(b.frame_id+delta);
      r.append(a);r.append(b);const auto s=wait(r);
      check(!s.complete&&s.error.find("transport sequence")!=std::string::npos,"transport gap/replay accepted");}
    {auto opt=options;opt.require_all_cameras=false;Raw8Recorder r(parent,opt);
      auto a=image(2,10),b=image(2,11);a.frame_id=UINT32_MAX;b.frame_id=0;
      r.append(a);r.append(b);const auto s=wait(r);
      check(s.complete&&validateRaw8Dataset(s.root,{},{}).valid,"transport rollover rejected");}
    {Raw8Recorder r(parent,options);r.append(image(0,0));r.append(image(0,2));const auto s=wait(r);
      check(!s.complete&&!s.error.empty(),"source gap accepted");}
    {Raw8Recorder r(parent,options);for(unsigned c=0;c<4;c++){
        auto i=image(c,0);if(c==1){i.encoded[20]^=1;native8_fixture::sealHeader(i.encoded);}r.append(i);}
      const auto s=wait(r);check(!s.complete&&!s.error.empty(),"bad decoded CRC accepted");}
    {options.before_write=[](){throw std::runtime_error("injected disk failure");};
      Raw8Recorder r(parent,options);r.append(image(0,0));const auto s=wait(r);
      check(!s.complete&&s.error=="injected disk failure","write error hidden");}
    {options={};options.fps=4;bool rejected=false;try{Raw8Recorder r(parent,options);}catch(...){rejected=true;}
      check(rejected,"rate above2FPS accepted");}
    std::cout<<"PASS RAW8 writer:2FPS fixture, no overwrite, full readback CRC, bounded non-evicting queues, gaps and I/O failures\n";
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
