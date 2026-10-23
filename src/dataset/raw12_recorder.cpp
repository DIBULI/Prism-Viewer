#include "dataset/raw12_recorder.hpp"
#include "dataset/raw12_dataset.hpp"
#include "common/ui_text.hpp"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QStorageInfo>
#include <QUuid>
#include <QJsonDocument>
#include <QJsonObject>
#include <condition_variable>
#include <algorithm>
#include <deque>
#include <mutex>
#include <numeric>
#include <sstream>
#include <thread>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace prism_viewer::dataset {
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void syncFile(QFileDevice& file){
  require(file.flush(),"RAW12 disk flush failed");
#ifdef _WIN32
  require(_commit(file.handle())==0,"RAW12 disk sync failed");
#else
  require(fsync(file.handle())==0,"RAW12 disk sync failed");
#endif
}
void writeAll(QFileDevice& file,const char* p,qint64 size){
  while(size){const auto n=file.write(p,size);require(n>0,"RAW12 disk write failed");p+=n;size-=n;}
}
}
struct Raw12Recorder::Impl {
  using Image=prism::capture::ProfileImage;
  struct Item{std::vector<uint8_t> bytes;unsigned camera;uint32_t id;uint64_t stamp,before;};
  Raw12RecordingOptions options;
  mutable std::mutex mutex;
  std::condition_variable wake;
  std::deque<Item> queue;
  Raw12RecordingStatus state;
  std::array<uint64_t,4> pending{},offset{},stamp{};
  std::array<uint32_t,4> first{},last{};
  std::thread worker;
  explicit Impl(const std::filesystem::path& parent,Raw12RecordingOptions o):options(std::move(o)){
    require(options.fps==1||options.fps==2,"RAW12 recording supports only1 or2FPS");
    require(options.queue_frames&&options.queue_bytes&&options.reserve_bytes>=64*1024*1024ull,
            "Invalid RAW12 recording queue or storage reserve");
    const auto path=common::fromFilesystemPath(parent);
    require(QDir(path).exists(),"Recording parent directory does not exist");
    checkSpace(path,128*1024*1024);
    const auto id=QStringLiteral("capture-%1-%2").arg(QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss"),
        QUuid::createUuid().toString(QUuid::Id128).left(12));
    require(QDir(path).mkdir(id),"Cannot create unique RAW12 dataset");
    state.root=common::toFilesystemPath(QDir(path).filePath(id));
    publish(false);
    worker=std::thread([this]{run();});
  }
  ~Impl(){finish({});if(worker.joinable())worker.join();}
  void checkSpace(const QString& path,uint64_t bytes){
    const QStorageInfo disk(path);
    require(disk.isValid()&&disk.isReady()&&disk.bytesAvailable()>=0,"Cannot query recording storage");
    require(uint64_t(disk.bytesAvailable())>options.reserve_bytes&&
            bytes<uint64_t(disk.bytesAvailable())-options.reserve_bytes,"RAW12 storage reserve reached");
  }
  Raw12RecordingStatus status()const{std::lock_guard<std::mutex> lock(mutex);return state;}
  void finish(const std::string& error){
    std::lock_guard<std::mutex> lock(mutex);
    if(state.done||state.closing)return;
    state.closing=true;if(state.error.empty())state.error=error;wake.notify_all();
  }
  void fail(const std::string& error){
    std::lock_guard<std::mutex> lock(mutex);
    state.closing=true;if(state.error.empty())state.error=error;wake.notify_all();
  }
  void publish(bool complete){
    std::ostringstream text;
    {std::lock_guard<std::mutex> lock(mutex);
      const auto drops=std::accumulate(state.dropped.begin(),state.dropped.end(),uint64_t(0));
      text<<"format=prism-raw12-dataset-v1\ndevice_model=415M\ncodec=raw12-rice-lossless\n"
          <<"width=5120\nheight=3072\nsource_bits=12\ncamera_count=4\n"
          <<"fps_per_camera="<<options.fps<<"\ncomplete="<<complete<<"\nbytes="<<state.bytes
          <<"\ndrop_accounting=1\nhas_drops="<<bool(drops)<<"\nframes_complete="<<(!drops&&state.error.empty())
          <<"\nqueue_policy=drop_oldest_queued\ndropped_frames_total="<<drops
          <<"\ntiming=diagnostic-not-synchronized-exposure\nrecording_host=viewer\n";
      std::string error=state.error;for(char& c:error)if(c=='\n'||c=='\r')c=' ';
      text<<"error="<<error<<'\n';
      for(unsigned c=0;c<4;c++){
        const auto key="camera"+std::to_string(c)+"_";
        text<<key<<"images="<<state.frames[c]<<'\n'<<key<<"received="<<state.received[c]<<'\n'
            <<key<<"dropped="<<state.dropped[c]<<'\n'<<key<<"dropped_tail="<<pending[c]<<'\n'
            <<key<<"first_received_frame="<<first[c]<<'\n'<<key<<"last_received_frame="<<last[c]<<'\n';
      }
    }
    QSaveFile file(common::fromFilesystemPath(state.root/"dataset.info"));
    require(file.open(QIODevice::WriteOnly),"Cannot publish RAW12 manifest");
    const auto bytes=text.str();writeAll(file,bytes.data(),qint64(bytes.size()));syncFile(file);
    require(file.commit(),"Cannot commit RAW12 manifest");
  }
  void append(const Image& image) noexcept {
    try{
      std::lock_guard<std::mutex> lock(mutex);
      if(state.closing||state.done)return;
      using namespace prism::capture;
      const auto& b=image.encoded;const auto* p=b.data();
      require(image.format==kRaw12Rice&&image.camera<4&&b.size()>64&&b.size()<=kProfileImageLimit&&
        !std::memcmp(p,"PRG1",4)&&p[4]==1&&p[5]==64&&p[6]==12&&p[36]==2&&p[37]==image.camera&&
        rawLe32(p+8)==5120&&rawLe32(p+12)==3072&&rawLe32(p+56)==b.size()-64,
        "Invalid RAW12 recording frame");
      const auto c=image.camera;const auto id=rawLe32(p+24);
      const auto time=rawLe32(p+40)|(uint64_t(rawLe32(p+44))<<32);
      require(state.received[c]<UINT32_MAX&&(!state.received[c]||
        (id==last[c]+1u&&time>stamp[c])),"RAW12 source sequence/timestamp gap");
      if(!state.received[c])first[c]=id;
      ++state.received[c];last[c]=id;stamp[c]=time;
      // Reject an oversized incoming frame without destroying useful queued
      // work. The writer's already-popped item is never eligible for eviction.
      if(b.size()<=options.queue_bytes){
        while(!queue.empty()&&(queue.size()>=options.queue_frames||
              state.queued_bytes>options.queue_bytes-b.size())){
          const auto camera=queue.front().camera;
          const auto carry=queue.front().before+1;
          state.queued_bytes-=queue.front().bytes.size();queue.pop_front();
          ++state.dropped[camera];
          auto next=std::find_if(queue.begin(),queue.end(),[&](const Item& i){return i.camera==camera;});
          if(next!=queue.end())next->before+=carry;
          else pending[camera]+=carry;
        }
      }
      if(queue.size()>=options.queue_frames||b.size()>options.queue_bytes||
          state.queued_bytes>options.queue_bytes-b.size()){
        ++state.dropped[c];++pending[c];return;
      }
      queue.push_back({b,c,id,time,pending[c]});pending[c]=0;state.queued_bytes+=b.size();wake.notify_all();
    }catch(const std::exception& e){fail(e.what());}
    catch(...){fail("RAW12 recording allocation failure");}
  }
  void run()noexcept{
    try{
      std::array<QFile,4> files,indexes;
      for(unsigned c=0;c<4;c++){
        const auto base=state.root/("camera"+std::to_string(c));
        files[c].setFileName(common::fromFilesystemPath(base)+".prg-stream");
        indexes[c].setFileName(common::fromFilesystemPath(base)+".jsonl");
        require(files[c].open(QIODevice::WriteOnly|QIODevice::NewOnly)&&
                indexes[c].open(QIODevice::WriteOnly|QIODevice::NewOnly),"Cannot create RAW12 streams");
      }
      for(;;){
        Item item;
        {std::unique_lock<std::mutex> lock(mutex);wake.wait(lock,[&]{return state.closing||!queue.empty();});
          if(queue.empty())break;
          item=std::move(queue.front());queue.pop_front();state.queued_bytes-=item.bytes.size();}
        if(options.before_write)options.before_write();
        checkSpace(common::fromFilesystemPath(state.root),item.bytes.size()+65536);
        require(state.bytes<=options.limit_bytes&&item.bytes.size()<=options.limit_bytes-state.bytes,
                "RAW12 recording size limit reached");
        const auto c=item.camera;const auto* p=item.bytes.data();
        require(prism::capture::rawCrc32(p+64,item.bytes.size()-64)==prism::capture::rawLe32(p+48),
                "RAW12 encoded CRC mismatch");
        writeAll(files[c],reinterpret_cast<const char*>(p),qint64(item.bytes.size()));
        QJsonObject row{{"frame_id",double(item.id)},{"timestamp_us",double(item.stamp)},
          {"offset",double(offset[c])},{"bytes",double(item.bytes.size())},{"timing_mode",int(p[38])},
          {"raw_crc32",double(prism::capture::rawLe32(p+20))},{"encoded_crc32",double(prism::capture::rawLe32(p+48))},
          {"dropped_before",double(item.before)}};
        if(item.before)row.insert("drop_reason","disk_queue_full");
        auto json=QJsonDocument(row).toJson(QJsonDocument::Compact);json+='\n';
        writeAll(indexes[c],json.constData(),json.size());
        {std::lock_guard<std::mutex> lock(mutex);++state.frames[c];state.bytes+=item.bytes.size();offset[c]+=item.bytes.size();}
      }
      for(unsigned c=0;c<4;c++){syncFile(files[c]);syncFile(indexes[c]);files[c].close();indexes[c].close();}
      {std::lock_guard<std::mutex> lock(mutex);for(auto count:state.received)
        if(!count&&state.error.empty())state.error="A camera supplied no RAW12 recording frame";}
      publish(false);
      if(status().error.empty()){
        const auto result=verifyUnpublishedRaw12Dataset(state.root);
        require(result.valid,"RAW12 full disk readback/decoded CRC validation failed");
        publish(true);
        std::lock_guard<std::mutex> lock(mutex);state.complete=true;
      }
    }catch(const std::exception& e){fail(e.what());try{publish(false);}catch(...){} }
    catch(...){fail("RAW12 writer failure");}
    {std::lock_guard<std::mutex> lock(mutex);queue.clear();state.queued_bytes=0;state.done=true;state.closing=true;}
  }
};
Raw12Recorder::Raw12Recorder(const std::filesystem::path& parent,Raw12RecordingOptions options)
  :impl_(std::make_unique<Impl>(parent,std::move(options))){}
Raw12Recorder::~Raw12Recorder()=default;
void Raw12Recorder::append(const prism::capture::ProfileImage& image)noexcept{impl_->append(image);}
void Raw12Recorder::finish(const std::string& error){impl_->finish(error);}
Raw12RecordingStatus Raw12Recorder::status()const{return impl_->status();}
}
