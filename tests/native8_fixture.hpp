#pragma once
#include "prism/usb/profile_video.hpp"
#include <algorithm>
// Independent fixtures only, never a production CPU encoder or GPU fallback.
namespace native8_fixture {
inline void put(std::vector<uint8_t>& b,size_t at,uint32_t n){for(unsigned k=0;k<4;++k)b[at+k]=uint8_t(n>>(8*k));}
inline void sealHeader(std::vector<uint8_t>& b){put(b,76,0);put(b,76,prism::capture::rawHeaderCrc32(b.data(),80));}
inline void seal(std::vector<uint8_t>& b){put(b,56,uint32_t(b.size()-80));put(b,48,prism::capture::rawCrc32(b.data()+80,b.size()-80));sealHeader(b);}
inline std::vector<uint8_t> header(unsigned camera,uint32_t id,uint64_t stamp,uint32_t crc){
  std::vector<uint8_t>b(80);std::memcpy(b.data(),"PRG8",4);
  b[4]=1;b[5]=80;b[6]=8;b[36]=2;b[37]=uint8_t(camera);b[38]=3;
  put(b,8,5120);put(b,12,3072);put(b,16,prism::capture::kRaw8Bytes);put(b,20,crc);
  put(b,24,id);put(b,28,1000);put(b,32,2048);put(b,40,uint32_t(stamp));put(b,44,uint32_t(stamp>>32));
  put(b,60,256);put(b,64,prism::capture::kRaw8Bytes/256);return b;
}
inline std::vector<uint8_t> constant(unsigned camera,uint32_t id,uint64_t stamp){
  static const auto crc=[](){std::vector<uint8_t> p(prism::capture::kRaw8Bytes,0x83);return prism::capture::rawCrc32(p.data(),p.size());}();
  auto b=header(camera,id,stamp,crc);b.reserve(80+3*61440);
  for(unsigned i=0;i<61440;++i)b.insert(b.end(),{0,0x83,0x83});seal(b);return b;
}
inline std::vector<uint8_t> verbatim(const std::vector<uint8_t>& pixels,unsigned camera=2){
  if(pixels.size()!=prism::capture::kRaw8Bytes)throw std::runtime_error("fixture size");
  auto b=header(camera,19,2345678,prism::capture::rawCrc32(pixels.data(),pixels.size()));b.reserve(80+61440*257);
  for(size_t i=0;i<pixels.size();i+=256){b.push_back(8);b.insert(b.end(),pixels.begin()+i,pixels.begin()+i+256);}seal(b);return b;
}
}
