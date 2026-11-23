#pragma once
#include "dataset/gnss_dataset.hpp"
#include <sstream>
#include <iomanip>
namespace gnss_fixture {
inline std::string nmea(const std::string& body) {
  uint8_t crc=0;for(unsigned char c:body)crc^=c;
  std::ostringstream out;out<<'$'<<body<<'*'<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(crc);return out.str();
}
inline std::string adr(const std::string& solution,unsigned epoch) {
  const auto body="ADRNAVA,COM1,GPS,FINE,2400,"+std::to_string(epoch)+",0,0,18,0;SOL_COMPUTED,"+solution+
      ",31.1666667,121.5,20,10,WGS84,0.03,0.04,0.07,0,0,0,20,18,0,0,0,0,0,0,0,0,0,0,0,0,0,0";
  uint32_t crc=0;for(unsigned char c:body){crc^=c;for(int i=0;i<8;++i)crc=(crc>>1)^((crc&1)?0xedb88320u:0);}
  std::ostringstream out;out<<'#'<<body<<'*'<<std::hex<<std::setw(8)<<std::setfill('0')<<crc;return out.str();
}
inline void create(const std::filesystem::path& root) {
  std::vector<std::string> lines;
  for(int q:{1,5,4,0})lines.push_back(nmea("GNGGA,12000"+std::to_string(q)+".100,3110.00000,N,12130.00000,E,"+std::to_string(q)+",15,1.2,20,M,10,M,1.5,"));
  lines.push_back(adr("NARROW_FLOAT",200000));lines.push_back(adr("NARROW_INT",200300));
  lines.push_back("$GNGGA,bad*00");lines.push_back(nmea("GNGGA,120006.100,3110.00100,N,12130.00100,E,4,15,1.2,20,M,10,M,1.5,"));
  prism_gnss_dataset::Writer writer(root);
  for(size_t i=0;i<lines.size();++i){
    prism::GnssObservations batch;batch.session=42;batch.cursor=i+1;batch.device_monotonic_ms=1000+i*300;
    batch.gap=i==7;batch.records={{batch.cursor,batch.device_monotonic_ms,lines[i]}};
    writer.append(batch,1000000+i*300000);
  }
  writer.close();
}
}
