#pragma once
// Application-level dataset writer shared by Web and Viewer, not an SDK API.
// Store receiver epochs verbatim: Agent monotonic time is NOT measurement UTC.
#include "prism/usb/gnss_plot.hpp"
#include "prism/usb/gnss_raw.hpp"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <vector>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace prism_gnss_dataset {
inline std::string csv(const std::string& s) {
  std::string v = "\"";
  for (char c : s) { if (c == '"') v += '"'; v += c; }
  return v + '"';
}
inline std::string numeric(std::optional<double> v) {
  if (!v) return {};
  std::ostringstream s; s.imbue(std::locale::classic()); s << std::setprecision(15) << *v; return s.str();
}
struct Stats {
  uint64_t observations=0, gnss=0, rtk=0, quality=0, invalid_checksums=0;
  uint64_t gap_events=0, query_errors=0, bytes=0;
  uint64_t receiver_raw_bytes=0, cors_raw_bytes=0, raw_records=0, raw_gap_events=0, raw_query_errors=0;
};
inline const std::vector<std::string>& fileNames() {
  static const std::vector<std::string> names = {"gnss_observations.bin", "gnss_observations.csv",
      "gnss_receiver.csv", "gnss_receiver_rtk.csv", "gnss_quality.csv", "gnss_status.csv",
      "gnss_raw.bin", "gnss_raw.csv", "cors_rtcm.bin", "cors_rtcm.csv", "gnss_raw_status.csv"};
  return names;
}
class Writer {
  std::ofstream raw_, index_, gnss_, rtk_, quality_, status_;
  std::ofstream receiver_raw_, receiver_index_, cors_raw_, cors_index_, raw_status_;
  uint64_t raw_session_=0, raw_cursor_=0;
  Stats stats_;
  prism::gnss_plot::Model model_;
  uint64_t session_=0, cursor_=0, raw_offset_=0;
  static void open(std::ofstream& f, const std::filesystem::path& p) {
    if (std::filesystem::exists(p)) throw std::runtime_error("GNSS dataset output already exists");
    f.exceptions(std::ios::badbit | std::ios::failbit);
    f.imbue(std::locale::classic());
    f.open(p, std::ios::out | std::ios::binary | std::ios::trunc);
  }
  void text(std::ofstream& f, const std::string& s) { f << s; stats_.bytes += s.size(); }
  std::string prefix(uint64_t elapsed, uint64_t session, const prism::GnssObservation& r) const {
    return std::to_string(elapsed)+","+std::to_string(session)+","+std::to_string(r.sequence)+","+std::to_string(r.received_ms)+",";
  }
  void position(std::ofstream& out, const prism::gnss_plot::Position& p,
                const std::string& lead, const std::string& state, const std::string& age) {
    text(out, lead+csv(p.source)+","+csv(p.epoch)+","+
        csv(p.source=="GGA" ? "UTC-time-of-day" : "GPS-week:milliseconds")+","+
        (p.valid?"1":"0")+","+csv(p.solution)+","+(p.quality>=0?std::to_string(p.quality):"")+","+
        (p.satellites>=0?std::to_string(p.satellites):"")+","+
        numeric(p.valid?std::optional<double>(p.latitude):std::nullopt)+","+
        numeric(p.valid?std::optional<double>(p.longitude):std::nullopt)+","+numeric(p.valid?p.height:std::nullopt)+","+
        numeric(p.hdop)+","+numeric(p.north_sigma)+","+numeric(p.east_sigma)+","+numeric(p.up_sigma)+","+
        csv(state)+","+age+"\n");
  }
 public:
  explicit Writer(const std::filesystem::path& root) {
    // Check the entire output set before creating any files.
    for(const auto& name:fileNames()) if(std::filesystem::exists(root/name))
      throw std::runtime_error("GNSS dataset output already exists");
    open(receiver_raw_,root/"gnss_raw.bin"); open(receiver_index_,root/"gnss_raw.csv");
    open(cors_raw_,root/"cors_rtcm.bin"); open(cors_index_,root/"cors_rtcm.csv");
    open(raw_status_,root/"gnss_raw_status.csv");
    const std::string raw_head="recording_elapsed_us,agent_session,sequence,agent_received_monotonic_ms,byte_offset,byte_size,flags,lost_bytes\n";
    text(receiver_index_,raw_head); text(cors_index_,raw_head);
    text(raw_status_,"recording_elapsed_us,agent_session,cursor,agent_monotonic_ms,state,oldest,newest,channel,flags,lost_bytes\n");
    open(raw_,root/"gnss_observations.bin"); open(index_,root/"gnss_observations.csv");
    open(gnss_,root/"gnss_receiver.csv"); open(rtk_,root/"gnss_receiver_rtk.csv");
    open(quality_,root/"gnss_quality.csv"); open(status_,root/"gnss_status.csv");
    text(index_,"recording_elapsed_us,agent_session,sequence,agent_received_monotonic_ms,byte_offset,byte_size,checksum_valid\n");
    const std::string head="recording_elapsed_us,agent_session,sequence,agent_received_monotonic_ms,source,receiver_epoch,receiver_time_scale,valid,solution,quality,satellites_used,latitude_deg,longitude_deg,ellipsoid_height_m,hdop,north_sigma_m,east_sigma_m,up_sigma_m,solution_status,differential_age_s\n";
    text(gnss_,head); text(rtk_,head);
    text(quality_,"recording_elapsed_us,agent_session,sequence,agent_received_monotonic_ms,source,receiver_epoch,system_id,fix_dimension,pdop,hdop,vdop,rms_m,semi_major_sigma_m,semi_minor_sigma_m,ellipse_orientation_deg,north_sigma_m,east_sigma_m,up_sigma_m\n");
    text(status_,"recording_elapsed_us,agent_session,cursor,agent_monotonic_ms,state,cache_gap,observations\n");
  }
  void status(uint64_t elapsed, const prism::GnssObservations& b, const std::string& state) {
    text(status_,std::to_string(elapsed)+","+std::to_string(b.session)+","+std::to_string(b.cursor)+","+
        std::to_string(b.device_monotonic_ms)+","+csv(state)+","+(b.gap?"1":"0")+","+std::to_string(b.records.size())+"\n");
  }
  void queryError(uint64_t elapsed) {
    ++stats_.query_errors;
    prism::GnssObservations b; b.session=session_; b.cursor=cursor_;
    status(elapsed,b,"query_error");
  }
  void append(const prism::GnssObservations& b, uint64_t elapsed) {
    const bool changed=session_&&session_!=b.session;
    if (changed) { cursor_=0; model_=prism::gnss_plot::Model{}; }
    if (b.gap||changed) { ++stats_.gap_events; model_=prism::gnss_plot::Model{}; }
    session_=b.session;
    status(elapsed,b,b.gap||changed?"cache_gap":b.records.empty()?"no_new_data":"data");
    for (const auto& r:b.records) {
      if (r.sequence<=cursor_) continue;
      const bool valid=prism::gnss_plot::checksum(r.sentence);
      // Original sentence bytes including the checksum, without CR/LF added by us.
      text(raw_,r.sentence);
      const auto lead=prefix(elapsed,b.session,r);
      text(index_,lead+std::to_string(raw_offset_)+","+std::to_string(r.sentence.size())+","+(valid?"1":"0")+"\n");
      raw_offset_+=r.sentence.size(); ++stats_.observations; cursor_=r.sequence;
      if (!valid) { ++stats_.invalid_checksums; continue; }
      prism::GnssObservations one; one.session=b.session; one.cursor=r.sequence;
      one.device_monotonic_ms=r.received_ms; one.records.push_back(r);
      // Parse each original sample, not a periodically repeated cached last position.
      model_.apply(one); model_.clearTracks();
      const auto star=r.sentence.find('*');
      const auto fields=prism::gnss_plot::split(r.sentence.substr(1,star-1));
      const auto type=fields[0].size()==5?fields[0].substr(2):std::string{};
      if (type=="GGA"&&model_.gnss.sequence==r.sequence) {
        const auto age=fields.size()>13?numeric(prism::gnss_plot::number(fields[13],0)):"";
        position(gnss_,model_.gnss,lead,"",age); ++stats_.gnss;
      } else if (r.sentence.rfind("#ADRNAVA,",0)==0&&model_.rtk.sequence==r.sequence) {
        const auto semi=r.sentence.find(';');
        const auto f=prism::gnss_plot::split(r.sentence.substr(semi+1,star-semi-1));
        position(rtk_,model_.rtk,lead,f[0],""); ++stats_.rtk;
      } else if (type=="GSA"&&fields.size()>=18) {
        text(quality_,lead+csv(fields[0])+",,"+(fields.size()>18?fields[18]:"")+","+fields[2]+","+
            numeric(prism::gnss_plot::number(fields[15],0))+","+numeric(prism::gnss_plot::number(fields[16],0))+","+
            numeric(prism::gnss_plot::number(fields[17],0))+",,,,,,,\n"); ++stats_.quality;
      } else if (type=="GST"&&fields.size()>=9) {
        text(quality_,lead+csv(fields[0])+","+csv(fields[1])+",,,,,,"+
            numeric(prism::gnss_plot::number(fields[2],0))+","+numeric(prism::gnss_plot::number(fields[3],0))+","+
            numeric(prism::gnss_plot::number(fields[4],0))+","+numeric(prism::gnss_plot::number(fields[5],0,360))+","+
            numeric(prism::gnss_plot::number(fields[6],0))+","+numeric(prism::gnss_plot::number(fields[7],0))+","+
            numeric(prism::gnss_plot::number(fields[8],0))+"\n"); ++stats_.quality;
      }
    }
    cursor_=std::max(cursor_,b.cursor);
  }
  Stats stats() const { return stats_; }
  void rawStatus(const prism::GnssRawBatch& b,uint64_t elapsed,const char* state,
                 unsigned channel=0,uint32_t flags=0,uint64_t lost=0) {
    text(raw_status_,std::to_string(elapsed)+","+std::to_string(b.session)+","+
      std::to_string(b.cursor)+","+std::to_string(b.device_monotonic_ms)+","+state+","+
      std::to_string(b.oldest)+","+std::to_string(b.newest)+","+std::to_string(channel)+","+
      std::to_string(flags)+","+std::to_string(lost)+"\n");
  }
  void rawQueryError(uint64_t elapsed) {
    ++stats_.raw_query_errors; prism::GnssRawBatch b; b.session=raw_session_; b.cursor=raw_cursor_;
    rawStatus(b,elapsed,"query_error");
  }
  void appendRaw(const prism::GnssRawBatch& b,uint64_t elapsed) {
    bool changed=raw_session_ && b.session!=raw_session_;
    if(changed) raw_cursor_=0;
    if(b.gap||changed) { ++stats_.raw_gap_events; rawStatus(b,elapsed,"cache_gap"); }
    raw_session_=b.session;
    if(b.records.empty()) rawStatus(b,elapsed,"no_new_data");
    for(const auto& r:b.records) {
      if(r.sequence<=raw_cursor_) continue;
      if(r.flags || r.lost_bytes) {
        ++stats_.raw_gap_events;
        auto mark=b; mark.cursor=r.sequence; mark.device_monotonic_ms=r.received_ms;
        rawStatus(mark,elapsed,"source_gap",unsigned(r.channel),r.flags,r.lost_bytes);
      }
      const bool receiver=r.channel==prism::GnssRawChannel::Receiver;
      if(r.channel!=prism::GnssRawChannel::Both) {
        auto& out=receiver?receiver_raw_:cors_raw_;
        auto& idx=receiver?receiver_index_:cors_index_;
        auto& offset=receiver?stats_.receiver_raw_bytes:stats_.cors_raw_bytes;
        if(!r.data.empty()) out.write(reinterpret_cast<const char*>(r.data.data()),std::streamsize(r.data.size()));
        stats_.bytes+=r.data.size();
        text(idx,std::to_string(elapsed)+","+std::to_string(b.session)+","+std::to_string(r.sequence)+","+
          std::to_string(r.received_ms)+","+std::to_string(offset)+","+std::to_string(r.data.size())+","+
          std::to_string(r.flags)+","+std::to_string(r.lost_bytes)+"\n");
        offset+=r.data.size();
      }
      ++stats_.raw_records; raw_cursor_=r.sequence;
    }
    raw_cursor_=std::max(raw_cursor_,b.cursor);
  }
  void flush() { for(auto* f:{&raw_,&index_,&gnss_,&rtk_,&quality_,&status_,&receiver_raw_,&receiver_index_,&cors_raw_,&cors_index_,&raw_status_}) f->flush(); }
  void close() { for(auto* f:{&raw_,&index_,&gnss_,&rtk_,&quality_,&status_,&receiver_raw_,&receiver_index_,&cors_raw_,&cors_index_,&raw_status_}) if(f->is_open()) { f->flush(); f->close(); } }
};

// Host-side buffering: never write files on the USB receive/query thread.
class BufferedWriter {
  struct Job { prism::GnssObservations batch; uint64_t elapsed; bool error; prism::GnssRawBatch raw{}; bool is_raw=false; };
  Writer writer_;
  mutable std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<Job> jobs_;
  size_t queued_=0;
  bool done_=false, pending_gap_=false;
  bool pending_raw_gap_=false;
  Stats stats_;
  std::string error_;
  std::thread thread_;
  void run() noexcept {
    try {
      for (;;) {
        Job j;
        {
          std::unique_lock<std::mutex> l(mutex_);
          wake_.wait(l,[this]{return done_||!jobs_.empty();});
          if(jobs_.empty()) break;
          j=std::move(jobs_.front()); jobs_.pop_front();
          queued_-=sizeof(Job);
          for(const auto& r:j.batch.records) queued_-=sizeof(r)+r.sentence.size();
          for(const auto& r:j.raw.records) queued_-=sizeof(r)+r.data.size();
        }
        if(j.is_raw) {
          if(j.error) writer_.rawQueryError(j.elapsed); else writer_.appendRaw(j.raw,j.elapsed);
        } else if(j.error) writer_.queryError(j.elapsed); else writer_.append(j.batch,j.elapsed);
        writer_.flush();
        std::lock_guard<std::mutex> l(mutex_); stats_=writer_.stats();
      }
      writer_.close();
    } catch(const std::exception& e) {
      std::lock_guard<std::mutex> l(mutex_); error_=e.what();
    }
  }
 public:
  explicit BufferedWriter(const std::filesystem::path& root):writer_(root),stats_(writer_.stats()),thread_([this]{run();}) {}
  ~BufferedWriter() { finish(); }
  void append(prism::GnssObservations b,uint64_t elapsed,bool error=false) {
    // Host and device steady clocks have different epochs. Use only ages to
    // exclude pre-recording cache history; preserve exact receiver/device times.
    const uint64_t oldest=b.device_monotonic_ms>elapsed/1000?b.device_monotonic_ms-elapsed/1000:0;
    b.records.erase(std::remove_if(b.records.begin(),b.records.end(),[&](const auto& r){return r.received_ms<oldest;}),b.records.end());
    size_t bytes=sizeof(Job); for(const auto& r:b.records) bytes+=sizeof(r)+r.sentence.size();
    std::lock_guard<std::mutex> l(mutex_);
    if(done_||!error_.empty()) return;
    if(queued_+bytes>2u*1024u*1024u) { pending_gap_=true; return; }
    if(!error) { b.gap=b.gap||pending_gap_; pending_gap_=false; }
    jobs_.push_back({std::move(b),elapsed,error}); queued_+=bytes; wake_.notify_one();
  }
  void finish() {
    { std::lock_guard<std::mutex> l(mutex_);
      if(!done_&&pending_gap_) { prism::GnssObservations b; b.gap=true; jobs_.push_back({b,0,false}); queued_+=sizeof(Job); }
      if(!done_&&pending_raw_gap_) { Job j{}; j.is_raw=true; j.raw.gap=true; jobs_.push_back(std::move(j)); queued_+=sizeof(Job); }
      done_=true; wake_.notify_one(); }
    if(thread_.joinable()) thread_.join();
  }
  Stats stats() const { std::lock_guard<std::mutex> l(mutex_); return stats_; }
  void appendRaw(prism::GnssRawBatch b,uint64_t elapsed,bool error=false) {
    const uint64_t oldest=b.device_monotonic_ms>elapsed/1000?b.device_monotonic_ms-elapsed/1000:0;
    b.records.erase(std::remove_if(b.records.begin(),b.records.end(),[&](const auto& r){return r.received_ms<oldest;}),b.records.end());
    size_t bytes=sizeof(Job); for(const auto& r:b.records) bytes+=sizeof(r)+r.data.size();
    std::lock_guard<std::mutex> l(mutex_);
    if(done_||!error_.empty()) return;
    if(queued_+bytes>2u*1024u*1024u) {pending_raw_gap_=true;return;}
    if(!error) {b.gap=b.gap||pending_raw_gap_;pending_raw_gap_=false;}
    Job j{};j.elapsed=elapsed;j.error=error;j.is_raw=true;j.raw=std::move(b);
    jobs_.push_back(std::move(j));queued_+=bytes;wake_.notify_one();
  }
  std::string error() const { std::lock_guard<std::mutex> l(mutex_); return error_; }
};
} // namespace prism_gnss_dataset
