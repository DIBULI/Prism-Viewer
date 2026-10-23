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
