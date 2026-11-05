#pragma once
#include "prism/usb/profile_video.hpp"
#include "native8_fixture.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <filesystem>
#include <fstream>

namespace raw_fixture {
inline void save(const std::filesystem::path& path, const std::string& bytes) {
  std::ofstream file(path, std::ios::binary); file.write(bytes.data(), std::streamsize(bytes.size()));
}
inline std::vector<uint8_t> frame(unsigned camera, uint32_t id, uint64_t stamp) {
  return native8_fixture::constant(camera,id,stamp);
}
inline std::string create(const std::filesystem::path& root, bool drops=false, bool empty_camera=false,
                          unsigned timing=3,bool unknown=false,unsigned stride=1) {
  std::filesystem::create_directories(root);
  std::string info="format=prism-raw8-dataset-v1\ndevice_model=415RSV0\ncodec=raw8-rice-lossless\ncomplete=1\nwidth=5120\nheight=3072\nsource_bits=8\ncamera_count=4\nfps_per_camera=1\n";
  if(drops)info+="drop_accounting=1\nhas_drops=1\nframes_complete=0\n";
  if(stride>1){
    info.replace(info.find("fps_per_camera=1"),16,stride==2?"fps_per_camera=1":"fps_per_camera=0.5");
    info+="sampling=source_cadence\nsource_fps_per_camera=2\nsource_milli_fps=2000\nsource_clock_hz=25000000\nsource_period_cycles=12500000\nframe_stride="+std::to_string(stride)+"\n";
    if(!drops)info+="drop_accounting=1\nhas_drops=0\nframes_complete=1\n";
  }
  uint64_t total=0;
  for(unsigned c=0;c<4;++c){
    const unsigned count=empty_camera&&c==1?0:c==3?3:2;
    std::string stream,index;uint64_t offset=0;
    for(unsigned i=0;i<count;++i){
      const unsigned before=drops&&c==0?1:0;
      const uint32_t id=10+stride*(i+(drops&&c==0?i+1:0));
      const unsigned mode=unknown&&c==2&&i==0?0:timing;
      const uint64_t stamp=mode?1000000ULL+(id-10)*(stride>1?500000ULL:1000000ULL)+(timing==4?0:c*1000):0;
      auto bytes=frame(c,id,stamp);
      bytes[38]=uint8_t(mode);
      native8_fixture::put(bytes,52,mode==4?id:0);native8_fixture::sealHeader(bytes);
      QJsonObject row{{"offset",double(offset)},{"bytes",int(bytes.size())},{"frame_id",double(id)},
          {"timestamp_us",double(stamp)},{"timing_mode",int(mode)},
          {"raw_crc32",double(prism::capture::rawLe32(bytes.data()+20))},
          {"encoded_crc32",double(prism::capture::rawLe32(bytes.data()+48))}};
      row.insert("trigger_sequence",mode==4?int(id):0);
      if(drops||stride>1)row.insert("dropped_before",int(before));
      if(before)row.insert("drop_reason","disk_queue_full");
      index+=QJsonDocument(row).toJson(QJsonDocument::Compact).toStdString()+"\n";
      stream.append(reinterpret_cast<const char*>(bytes.data()),bytes.size());offset+=bytes.size();
    }
    const auto base="camera"+std::to_string(c);
    save(root/(base+".jsonl"),index);save(root/(base+".pr8-stream"),stream);total+=offset;
    info+=base+"_images="+std::to_string(count)+"\n";
    if(drops||stride>1){const unsigned n=drops?(c==0?3:!count?2:0):0,received=(count+n)*stride;
      if(stride>1)info+=base+"_sampled_out="+std::to_string(received-count-n)+"\n";
      info+=base+"_received="+std::to_string(received)+"\n"+base+"_dropped="+std::to_string(n)+"\n"+
          base+"_dropped_tail="+std::to_string(drops?(c==0?1:!count?2:0):0)+"\n"+
          base+"_first_received_frame=10\n"+base+"_last_received_frame="+std::to_string(10+received-1)+"\n";
    }
  }
  info+="bytes="+std::to_string(total)+"\n"; save(root/"dataset.info",info);return info;
}
}
