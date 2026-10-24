#pragma once
#include "prism/usb/profile_video.hpp"
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
  std::vector<uint8_t> bytes(64), pixels(5120u*3072u*2u);
  for (size_t i=0; i<pixels.size()/2; ++i) {pixels[2*i]=0x30; pixels[2*i+1]=0x08;}
  for (size_t i=0; i<pixels.size()/2; i+=256) {
    bytes.insert(bytes.end(), {0,0x30,0x08,0x30,0x08});
  }
  auto put=[&](size_t at,uint32_t n){for(unsigned i=0;i<4;++i)bytes[at+i]=uint8_t(n>>(8*i));};
  std::memcpy(bytes.data(),"PRG1",4);bytes[4]=1;bytes[5]=64;bytes[6]=12;
  bytes[36]=2;bytes[37]=uint8_t(camera);bytes[38]=3;
  put(8,5120);put(12,3072);put(16,23592960);put(20,prism::capture::rawCrc32(pixels.data(),pixels.size()));
  put(24,id);put(28,1000);put(32,2048);put(40,uint32_t(stamp));put(44,uint32_t(stamp>>32));
  put(48,prism::capture::rawCrc32(bytes.data()+64,bytes.size()-64));put(52,256);put(56,uint32_t(bytes.size()-64));
  return bytes;
}
inline std::string create(const std::filesystem::path& root, bool drops=false, bool empty_camera=false,
                          unsigned timing=3,bool unknown=false) {
  std::filesystem::create_directories(root);
  std::string info="format=prism-raw12-dataset-v1\ndevice_model=415M\ncodec=raw12-rice-lossless\ncomplete=1\nwidth=5120\nheight=3072\nsource_bits=12\ncamera_count=4\nfps_per_camera=1\n";
  if(drops)info+="drop_accounting=1\nhas_drops=1\nframes_complete=0\n";
  uint64_t total=0;
  for(unsigned c=0;c<4;++c){
    const unsigned count=empty_camera&&c==1?0:c==3?3:2;
    std::string stream,index;uint64_t offset=0;
    for(unsigned i=0;i<count;++i){
      const unsigned before=drops&&c==0?1:0;
      const uint32_t id=10+i+(drops&&c==0?i+1:0);
      const unsigned mode=unknown&&c==2&&i==0?0:timing;
      const uint64_t stamp=mode?1000000ULL+(id-10)*1000000ULL+(timing==4?0:c*1000):0;
      auto bytes=frame(c,id,stamp);
      bytes[38]=uint8_t(mode);
      QJsonObject row{{"offset",double(offset)},{"bytes",int(bytes.size())},{"frame_id",double(id)},
          {"timestamp_us",double(stamp)},{"timing_mode",int(mode)},
          {"raw_crc32",double(prism::capture::rawLe32(bytes.data()+20))},
          {"encoded_crc32",double(prism::capture::rawLe32(bytes.data()+48))}};
      if(drops)row.insert("dropped_before",int(before));
      if(before)row.insert("drop_reason","disk_queue_full");
      index+=QJsonDocument(row).toJson(QJsonDocument::Compact).toStdString()+"\n";
      stream.append(reinterpret_cast<const char*>(bytes.data()),bytes.size());offset+=bytes.size();
    }
    const auto base="camera"+std::to_string(c);
    save(root/(base+".jsonl"),index);save(root/(base+".prg-stream"),stream);total+=offset;
    info+=base+"_images="+std::to_string(count)+"\n";
    if(drops){const unsigned n=c==0?3:!count?2:0,received=count+n;
      info+=base+"_received="+std::to_string(received)+"\n"+base+"_dropped="+std::to_string(n)+"\n"+
          base+"_dropped_tail="+std::to_string(c==0?1:!count?2:0)+"\n"+
          base+"_first_received_frame=10\n"+base+"_last_received_frame="+std::to_string(10+received-1)+"\n";
    }
  }
  info+="bytes="+std::to_string(total)+"\n"; save(root/"dataset.info",info);return info;
}
}
