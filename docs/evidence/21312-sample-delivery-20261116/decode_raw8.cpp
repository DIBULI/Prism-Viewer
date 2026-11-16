#include "raw8_header.hpp"
#include "raw8_rice.hpp"
#include <fstream>
#include <iostream>
#include <vector>
#include <string>

// Developer reference: one indexed PRG8 frame -> binary PGM with native Bayer8.
// No rescaling, demosaic, gamma or white balance. Does not connect to hardware.
int main(int argc,char** argv) {
  try {
    if(argc!=5)throw std::runtime_error("usage: decode_raw8 STREAM OFFSET BYTES OUTPUT.pgm");
    const auto offset=std::stoull(argv[2]), bytes=std::stoull(argv[3]);
    if(bytes<=80||bytes>25*1024*1024)throw std::runtime_error("frame length out of bounds");
    std::ifstream in(argv[1],std::ios::binary);
    if(!in)throw std::runtime_error("cannot open input");
    in.seekg(0,std::ios::end);const auto size=in.tellg();
    if(size<0||offset>uint64_t(size)||bytes>uint64_t(size)-offset)
      throw std::runtime_error("indexed frame exceeds input");
    in.seekg(std::streamoff(offset));
    std::vector<uint8_t> encoded(size_t(bytes),0);
    in.read(reinterpret_cast<char*>(encoded.data()),std::streamsize(encoded.size()));
    if(size_t(in.gcount())!=encoded.size())throw std::runtime_error("short read");
    const auto h=prism::capture::parseRaw8Header(encoded.data(),encoded.size(),encoded.size(),prism::capture::kRaw8Rice);
    std::vector<uint8_t> pixels(h.raw_bytes);
    prism::capture::decodeRaw8RicePayload(encoded.data()+80,encoded.size()-80,pixels.data(),pixels.size());
    // Sample files contain metadata checksums. This example checks structural
    // bounds/header only and is not a full payload integrity verifier.
    std::ifstream existing(argv[4],std::ios::binary);
    if(existing)throw std::runtime_error("output already exists");
    std::ofstream out(argv[4],std::ios::binary);
    out<<"P5\n"<<h.width<<' '<<h.height<<"\n255\n";
    out.write(reinterpret_cast<const char*>(pixels.data()),std::streamsize(pixels.size()));
    out.close();if(!out)throw std::runtime_error("output write failed");
    std::cout<<"camera="<<unsigned(h.camera)<<" width="<<h.width<<" height="<<h.height
      <<" frame_id="<<h.frame_id<<" timestamp_us="<<h.timestamp_us<<"\n";
    return 0;
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
