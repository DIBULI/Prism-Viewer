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
#include <QCryptographicHash>
#include <condition_variable>
#include <algorithm>
#include <deque>
#include <mutex>
#include <numeric>
#include <sstream>
#include <thread>
#include <iomanip>
#include <locale>
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
constexpr const char* imuHeader="sensor_id,format,flags,sample_id,timestamp_us,timestamp_synced,fsync_event,fsync_delay_valid,sample_gap,ax_mg,ay_mg,az_mg,gx_mdps,gy_mdps,gz_mdps,temp_milli_c,host_elapsed_us\n";
constexpr const char* tumHeader="# Source timestamp[s] ax[m/s^2] ay[m/s^2] az[m/s^2] gx[rad/s] gy[rad/s] gz[rad/s]\n";
std::string imuCsv(const prism::ImuSample& s,uint64_t elapsed){
  std::ostringstream o;o.imbue(std::locale::classic());
  o<<unsigned(s.sensor_id)<<','<<unsigned(s.format)<<','<<s.flags<<','<<s.sample_id<<','<<s.timestamp_us
   <<','<<s.timestamp_synced<<','<<s.fsync_event<<','<<s.fsync_delay_valid<<','<<s.sample_gap;
  for(auto v:s.accel_mg)o<<','<<v;
  for(auto v:s.gyro_mdps)o<<','<<v;
  o<<','<<s.temp_milli_c<<','<<elapsed<<'\n';return o.str();
}
std::string imuTum(const prism::ImuSample& s){
  std::ostringstream o;o.imbue(std::locale::classic());
  o<<s.timestamp_us/1000000<<'.'<<std::setw(6)<<std::setfill('0')<<s.timestamp_us%1000000;
  o<<std::fixed<<std::setprecision(9);
  for(auto v:s.accel_mg)o<<' '<<double(v)*0.00980665;
  for(auto v:s.gyro_mdps)o<<' '<<double(v)*(3.14159265358979323846/180000.0);
  o<<'\n';return o.str();
}
void hashText(QCryptographicHash& h,const std::string& s){
  // QByteArrayView overload is Qt6-only; Linux packages retain Qt5.12.
  h.addData(QByteArray(s.data(),int(s.size())));
}
void verifyFileHash(const std::filesystem::path& path,const QByteArray& expected){
  QFile f(common::fromFilesystemPath(path));require(f.open(QIODevice::ReadOnly),"Cannot read back IMU file");
  QCryptographicHash h(QCryptographicHash::Sha256);
  while(!f.atEnd()){
    const auto b=f.read(65536);require(!b.isEmpty()||f.error()==QFileDevice::NoError,"IMU readback failed");h.addData(b);
  }
  require(h.result()==expected,"IMU disk readback does not match received samples");
}
}
struct Raw12Recorder::Impl {
  using Image=prism::capture::ProfileImage;
  struct Item{std::vector<uint8_t> bytes;unsigned camera;uint32_t id;uint64_t stamp,before;};
  Raw12RecordingOptions options;
  mutable std::mutex mutex;
  std::condition_variable wake;
  std::deque<Item> queue;
  struct ImuItem{unsigned sensor;std::string csv,tum;};
  std::deque<ImuItem> imu_queue;
  QCryptographicHash imu_hash{QCryptographicHash::Sha256};
  std::array<QCryptographicHash,2> tum_hash{{QCryptographicHash(QCryptographicHash::Sha256),QCryptographicHash(QCryptographicHash::Sha256)}};
  std::array<uint64_t,2> imu_stamp{};
  std::array<uint16_t,2> imu_sequence{};
  const std::chrono::steady_clock::time_point started=std::chrono::steady_clock::now();
  Raw12RecordingStatus state;
  std::array<uint64_t,4> pending{},offset{},stamp{};
  std::array<uint32_t,4> first{},last{};
  std::array<uint8_t,4> timing{};
  std::thread worker;
  explicit Impl(const std::filesystem::path& parent,Raw12RecordingOptions o):options(std::move(o)){
    require(options.fps==1||options.fps==2,"RAW12 recording supports only1 or2FPS");
    require(options.queue_frames&&options.queue_bytes&&options.reserve_bytes>=64*1024*1024ull,
            "Invalid RAW12 recording queue or storage reserve");
    require(!(options.required_imu_mask&~3u)&&options.imu_queue_samples,"Invalid IMU recording limits");
    hashText(imu_hash,imuHeader);for(auto& h:tum_hash)hashText(h,tumHeader);
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
    if(state.done)return;
    state.closing=true;if(state.error.empty())state.error=error;wake.notify_all();
  }
  void fail(const std::string& error){
    std::lock_guard<std::mutex> lock(mutex);
    state.closing=true;if(state.error.empty())state.error=error;wake.notify_all();
  }
  void publish(bool complete){
    std::ostringstream text;
    // Serialize the final error check, manifest commit and terminal state.
    // A companion failure can arrive while the full readback is in progress.
    const bool finalizing=complete;
    std::lock_guard<std::mutex> lock(mutex);
    complete=complete&&state.error.empty();
      const auto drops=std::accumulate(state.dropped.begin(),state.dropped.end(),uint64_t(0));
      text<<"format=prism-raw12-dataset-v1\ndevice_model=415M\ncodec=raw12-rice-lossless\n"
          <<"width=5120\nheight=3072\nsource_bits=12\ncamera_count=4\n"
          <<"fps_per_camera="<<options.fps<<"\ncomplete="<<complete<<"\nbytes="<<state.bytes
          <<"\ndrop_accounting=1\nhas_drops="<<bool(drops)<<"\nframes_complete="<<(!drops&&state.error.empty())
          <<"\nqueue_policy=drop_oldest_queued\ndropped_frames_total="<<drops
          <<"\ntiming=per-image-discriminator-device-clock\nrecording_host=viewer\n";
      text<<"allow_absent_cameras="<<!options.require_all_cameras<<'\n';
      text<<"fps_reference=configured-not-measured\ncompanion_streams="<<options.companion_streams<<'\n';
      text<<"imu_required_mask="<<options.required_imu_mask<<"\nimu_timestamp_domain="<<options.imu_timestamp_domain
          <<"\nimu_queue_policy=fail_without_eviction\nimu_queue_peak="<<state.imu_queue_peak
          <<"\nimu_metadata_sha256="<<imu_hash.result().toHex().constData()<<'\n';
      for(unsigned i=0;i<2;i++)text<<"imu"<<i<<"_received="<<state.imu_received[i]<<"\nimu"<<i<<"_samples="<<state.imu_samples[i]
          <<"\nimu"<<i<<"_tum_sha256="<<tum_hash[i].result().toHex().constData()<<'\n';
      std::string error=state.error;for(char& c:error)if(c=='\n'||c=='\r')c=' ';
      text<<"error="<<error<<'\n';
      for(unsigned c=0;c<4;c++){
        const auto key="camera"+std::to_string(c)+"_";
        text<<key<<"images="<<state.frames[c]<<'\n'<<key<<"received="<<state.received[c]<<'\n'
            <<key<<"dropped="<<state.dropped[c]<<'\n'<<key<<"dropped_tail="<<pending[c]<<'\n'
            <<key<<"first_received_frame="<<first[c]<<'\n'<<key<<"last_received_frame="<<last[c]<<'\n';
      }
    QSaveFile file(common::fromFilesystemPath(state.root/"dataset.info"));
    require(file.open(QIODevice::WriteOnly),"Cannot publish RAW12 manifest");
    const auto bytes=text.str();writeAll(file,bytes.data(),qint64(bytes.size()));syncFile(file);
    require(file.commit(),"Cannot commit RAW12 manifest");
    if(finalizing){state.complete=complete;state.done=true;state.closing=true;}
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
      require((p[38]==0||p[38]==3||p[38]==4)&&validProfileTiming(p[38],time),"Invalid RAW12 image timing");
      require(state.received[c]<UINT32_MAX&&(!state.received[c]||
        (id==last[c]+1u&&(!p[38]||p[38]!=timing[c]||time>stamp[c]))),"RAW12 source sequence/timestamp gap");
      if(!state.received[c])first[c]=id;
      ++state.received[c];last[c]=id;stamp[c]=time;timing[c]=p[38];
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
  void appendImu(const prism::ImuSample& sample) noexcept {
    try{
      std::lock_guard<std::mutex> lock(mutex);
      if(state.closing||state.done||(!options.required_imu_mask&&!options.record_any_imu))return;
      require(sample.sensor_id<2,"Invalid IMU sensor id");
      const auto c=sample.sensor_id;
      require(options.record_any_imu||(options.required_imu_mask&(1u<<c)),"Unexpected IMU appeared during recording");
      require(imu_queue.size()<options.imu_queue_samples,"IMU recording queue full; recording stopped without eviction");
      const auto elapsed=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-started).count();
      ImuItem item{c,imuCsv(sample,uint64_t(elapsed)),imuTum(sample)};
      imu_queue.push_back(item);
      hashText(imu_hash,item.csv);hashText(tum_hash[c],item.tum);
      const bool first_sample=state.imu_received[c]++==0;
      state.imu_queue_peak=std::max(state.imu_queue_peak,uint64_t(imu_queue.size()));
      // Preserve the offending sample as evidence before failing. Neither a
      // PPS re-anchor nor UI smoothing may rewrite a recorded timestamp.
      const bool valid=(options.record_any_imu||sample.timestamp_synced)&&sample.timestamp_us&&!sample.sample_gap&&
          (first_sample||(uint16_t(sample.sample_id-imu_sequence[c])==1&&sample.timestamp_us>imu_stamp[c]));
      imu_sequence[c]=uint16_t(sample.sample_id);imu_stamp[c]=sample.timestamp_us;
      if(!valid){state.closing=true;if(state.error.empty())state.error="IMU sequence/gap/time validation failed; original samples retained";}
      wake.notify_all();
    }catch(const std::exception& e){fail(e.what());}
    catch(...){fail("IMU recording allocation failure");}
  }
  void run()noexcept{
    try{
      std::array<QFile,4> files,indexes;
      QFile imu_file;std::array<QFile,2> tum_files;
      if(options.required_imu_mask||options.record_any_imu){
        imu_file.setFileName(common::fromFilesystemPath(state.root/"imu_metadata.csv"));
        require(imu_file.open(QIODevice::WriteOnly|QIODevice::NewOnly),"Cannot create original IMU metadata");
        writeAll(imu_file,imuHeader,qint64(std::strlen(imuHeader)));
        for(unsigned c=0;c<2;c++)if(options.record_any_imu||(options.required_imu_mask&(1u<<c))){
          tum_files[c].setFileName(common::fromFilesystemPath(state.root/("imu"+std::to_string(c)+".tum")));
          require(tum_files[c].open(QIODevice::WriteOnly|QIODevice::NewOnly),"Cannot create IMU TUM stream");
          writeAll(tum_files[c],tumHeader,qint64(std::strlen(tumHeader)));
        }
      }
      for(unsigned c=0;c<4;c++){
        const auto base=state.root/("camera"+std::to_string(c));
        files[c].setFileName(common::fromFilesystemPath(base)+".prg-stream");
        indexes[c].setFileName(common::fromFilesystemPath(base)+".jsonl");
        require(files[c].open(QIODevice::WriteOnly|QIODevice::NewOnly)&&
                indexes[c].open(QIODevice::WriteOnly|QIODevice::NewOnly),"Cannot create RAW12 streams");
      }
      for(;;){
        Item item;std::vector<ImuItem> imu_batch;
        {std::unique_lock<std::mutex> lock(mutex);wake.wait(lock,[&]{return state.closing||!queue.empty()||!imu_queue.empty();});
          if(queue.empty()&&imu_queue.empty())break;
          while(!imu_queue.empty()&&imu_batch.size()<256){imu_batch.push_back(std::move(imu_queue.front()));imu_queue.pop_front();}
          if(imu_batch.empty()){item=std::move(queue.front());queue.pop_front();state.queued_bytes-=item.bytes.size();}}
        if(options.before_write)options.before_write();
        if(!imu_batch.empty()){
          checkSpace(common::fromFilesystemPath(state.root),65536);
          for(const auto& imu:imu_batch){
            writeAll(imu_file,imu.csv.data(),qint64(imu.csv.size()));
            writeAll(tum_files[imu.sensor],imu.tum.data(),qint64(imu.tum.size()));
            std::lock_guard<std::mutex> lock(mutex);++state.imu_samples[imu.sensor];
          }
          continue;
        }
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
      if(options.required_imu_mask||options.record_any_imu){
        syncFile(imu_file);imu_file.close();
        verifyFileHash(state.root/"imu_metadata.csv",imu_hash.result());
        for(unsigned c=0;c<2;c++)if(options.record_any_imu||(options.required_imu_mask&(1u<<c))){
          syncFile(tum_files[c]);tum_files[c].close();
          verifyFileHash(state.root/("imu"+std::to_string(c)+".tum"),tum_hash[c].result());
          require((!(options.required_imu_mask&(1u<<c))||state.imu_received[c])&&state.imu_received[c]==state.imu_samples[c],"Required IMU samples missing on disk");
        }
      }
      {std::lock_guard<std::mutex> lock(mutex);for(auto count:state.received)
        if(options.require_all_cameras&&!count&&state.error.empty())state.error="A camera supplied no RAW12 recording frame";}
      publish(false);
      if(status().error.empty()){
        if(options.before_verify)options.before_verify();
        const auto result=verifyUnpublishedRaw12Dataset(state.root);
        require(result.valid,"RAW12 full disk readback/decoded CRC validation failed");
        publish(true);
      }
    }catch(const std::exception& e){fail(e.what());try{publish(false);}catch(...){} }
    catch(...){fail("RAW12 writer failure");}
    {std::lock_guard<std::mutex> lock(mutex);queue.clear();imu_queue.clear();state.queued_bytes=0;state.done=true;state.closing=true;}
  }
};
Raw12Recorder::Raw12Recorder(const std::filesystem::path& parent,Raw12RecordingOptions options)
  :impl_(std::make_unique<Impl>(parent,std::move(options))){}
Raw12Recorder::~Raw12Recorder()=default;
void Raw12Recorder::append(const prism::capture::ProfileImage& image)noexcept{impl_->append(image);}
void Raw12Recorder::appendImu(const prism::ImuSample& sample)noexcept{impl_->appendImu(sample);}
void Raw12Recorder::finish(const std::string& error){impl_->finish(error);}
Raw12RecordingStatus Raw12Recorder::status()const{return impl_->status();}
}
