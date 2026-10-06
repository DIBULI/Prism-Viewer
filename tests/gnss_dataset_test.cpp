#include "gnss_dataset.hpp"
#include <iostream>
#include <cstdlib>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

std::string nmea(std::string s) {
  unsigned c=0; for(char a:s)c^=uint8_t(a);
  std::ostringstream o; o<<'$'<<s<<'*'<<std::hex<<std::setw(2)<<std::setfill('0')<<c; return o.str();
}
std::string adr(const std::string& solution) {
  std::string s="ADRNAVA,COM1,GPS,FINE,2400,200000,0,0,18,0;SOL_COMPUTED,"+solution+
      ",31.1666667,121.5,20,10,WGS84,0.03,0.04,0.07,\"1\",1,0,20,18,0,0,0,0,0,0,0,0,0,0,0,0,0,0";
  uint32_t c=0; for(char a:s) {c^=uint8_t(a);for(int i=0;i<8;i++)c=(c>>1)^((c&1)?0xedb88320u:0);}
  std::ostringstream o;o<<'#'<<s<<'*'<<std::hex<<std::setw(8)<<std::setfill('0')<<c;return o.str();
}
std::vector<std::vector<std::string>> rows(const std::filesystem::path& p) {
  std::ifstream in(p); assert(in.good()); std::vector<std::vector<std::string>> out;
  for(std::string s;std::getline(in,s);) {
    std::vector<std::string> fields; std::string f; bool quote=false;
    for(size_t i=0;i<s.size();++i) { char c=s[i];
      if(c=='"') {if(quote&&i+1<s.size()&&s[i+1]=='"'){f+='"';++i;}else quote=!quote;}
      else if(c==','&&!quote) {fields.push_back(f);f.clear();}else f+=c;
    }
    assert(!quote);fields.push_back(f);out.push_back(fields);
    assert(out.back().size()==out.front().size());
  }
  return out;
}
int main() {
  auto root=std::filesystem::temp_directory_path()/
      ("prism-gnss-dataset-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directory(root);
  std::vector<std::string> messages;
  for(auto q:{1,5,4}) messages.push_back(nmea("GNGGA,120000.100,3110.00000,N,12130.00000,E,"+std::to_string(q)+",15,1.2,20,M,10,M,1.5,"));
  messages.push_back(nmea("GNGGA,120001.100,,,,,0,00,,,,,,,"));
  messages.push_back(adr("NARROW_FLOAT")); messages.push_back(adr("NARROW_INT"));
  messages.push_back(nmea("GPGSA,A,3,01,02,,,,,,,,,,,1.1,0.9,1.3,1"));
  messages.push_back(nmea("GNGST,120000.100,0.6,0.7,0.8,90,0.03,0.04,0.07"));
  messages.push_back(nmea("GPRMC,120000.100,A,3110.0,N,12130.0,E,0,0,010126,,,A"));
  messages.push_back(nmea("GPGSV,1,1,01,01,45,000,42,1"));
  messages.push_back("$GNGGA,bad*00");
  prism::GnssObservations b;b.session=123;b.device_monotonic_ms=20000;
  for(const auto& m:messages) b.records.push_back({++b.cursor,1000+b.cursor,m});
  {
    prism_gnss_dataset::Writer w(root);w.append(b,30000000);w.append(b,31000000);
    auto s=w.stats();assert(s.observations==messages.size()&&s.gnss==4&&s.rtk==2&&s.quality==2&&s.invalid_checksums==1);
    b.records.clear();b.gap=true;w.append(b,32000000);w.queryError(33000000);
    b.session=124;b.cursor=1;b.gap=false;b.records={{1,20000,messages[0]}};w.append(b,34000000);
    assert(w.stats().gap_events==2&&w.stats().query_errors==1&&w.stats().gnss==5);w.close();
  }
  auto g=rows(root/"gnss_receiver.csv");assert(g.size()==6&&g[0].size()==20);
  assert(g[1][8]=="SINGLE"&&g[2][8]=="FLOAT"&&g[3][8]=="FIX");
  assert(g[1][10]=="15"&&g[1][14]=="1.2"&&g[1][13]=="30"&&g[1][19]=="1.5");
  assert(g[4][7]=="0"&&g[4][11].empty()&&g[4][12].empty());
  auto r=rows(root/"gnss_receiver_rtk.csv");assert(r.size()==3&&r[1][8]=="NARROW_FLOAT"&&r[2][8]=="NARROW_INT");
  assert(r[1][5]=="2400:200000"&&r[1][15]=="0.03"&&r[1][16]=="0.04"&&r[1][17]=="0.07");
  auto q=rows(root/"gnss_quality.csv");assert(q.size()==3&&q[0].size()==18);
  assert(q[1][6]=="1"&&q[1][7]=="3"&&q[1][8]=="1.1"&&q[1][9]=="0.9"&&q[1][10]=="1.3"&&q[1][11].empty());
  assert(q[2][6].empty()&&q[2][10].empty()&&q[2][11]=="0.6"&&q[2][17]=="0.07");
  rows(root/"gnss_status.csv"); auto idx=rows(root/"gnss_observations.csv");
  std::ifstream raw(root/"gnss_observations.bin",std::ios::binary);
  for(size_t i=1;i<idx.size();++i) { std::string s(std::stoull(idx[i][5]),' ');raw.seekg(std::stoull(idx[i][4]));raw.read(s.data(),s.size()); assert(s==messages[(i-1)%messages.size()]); }
  bool collision=false;try{prism_gnss_dataset::Writer w(root);}catch(...){collision=true;}assert(collision);
  auto buffered=root/"buffered";std::filesystem::create_directory(buffered);
  {
    prism_gnss_dataset::BufferedWriter w(buffered);
    b.session=200;b.cursor=3;b.device_monotonic_ms=10000;
    b.records={{1,9000,messages[0]},{2,9995,messages[1]},{3,10000,messages[2]}};
    w.append(b,10000);w.append(b,20000);w.append({},25000,true);w.finish();
    assert(w.error().empty()&&w.stats().observations==2&&w.stats().gnss==2&&w.stats().query_errors==1);
  }
  assert(rows(buffered/"gnss_receiver.csv").size()==3);
  std::filesystem::remove_all(root);
  std::cout<<"GNSS/RTK recording: solutions, invalid fixes, DOP/GST precision, raw indexes, duplicate/gap/restart, checksum, history boundary and async drain passed\n";
}
