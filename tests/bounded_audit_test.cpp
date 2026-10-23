#include "ui/bounded_camera_audit.hpp"
#include "prism/usb/profile_video.hpp"
#include <chrono>
#include <future>
#include <fstream>
#include <iostream>
#include <vector>
static void require(bool yes){if(!yes)throw std::runtime_error("bounded audit regression");}
struct Item {unsigned camera=0,value=0;};
int main(int argc,char** argv){
  std::promise<void> entered,release;auto gate=release.get_future().share();
  std::vector<unsigned> seen;
  BoundedCameraAudit<Item> q([&](const Item& x){if(x.value==0){entered.set_value();gate.wait();}seen.push_back(x.value);});
  q.submit({0,0});entered.get_future().wait();q.submit({0,1});q.submit({0,2});
  bool full=false;try{q.submit({0,3});}catch(const std::runtime_error&){full=true;}
  release.set_value();bool reported=false;try{q.finish();}catch(const std::runtime_error&){reported=true;}
  require(full&&reported&&seen==std::vector<unsigned>({0,1,2}));
  {
    BoundedCameraAudit<Item> fail([](const Item&){throw std::runtime_error("decode CRC failed");});
    fail.submit({0,0});reported=false;try{fail.finish();}catch(const std::runtime_error&){reported=true;}require(reported);
  }
  std::array<std::vector<unsigned>,4> lanes;
  for(unsigned batch=0;batch<30;++batch){
    BoundedCameraAudit<Item> ordered([&](const Item& x){lanes[x.camera].push_back(x.value);});
    for(unsigned i=0;i<2;++i)for(unsigned c=0;c<4;++c)ordered.submit({c,batch*2+i});
    ordered.finish();
  }
  for(auto& lane:lanes){require(lane.size()==60);for(unsigned i=0;i<60;++i)require(lane[i]==i);}
  if(argc==5){
    std::array<prism::capture::ProfileImage,4> images;
    for(unsigned c=0;c<4;++c){auto& im=images[c];im.camera=c;im.format=4;std::ifstream f(argv[c+1],std::ios::binary);require(bool(f));im.encoded.assign(std::istreambuf_iterator<char>(f),{});}
    std::array<unsigned,4> verified{};
    auto started=std::chrono::steady_clock::now();
    BoundedCameraAudit<prism::capture::ProfileImage> raw([&](const auto& im){auto r=prism::capture::decodeRaw12(im.encoded,im.format);require(r.camera==im.camera&&r.width==5120&&r.height==3072);++verified[im.camera];});
    for(unsigned i=0;i<2;++i)for(const auto& im:images)raw.submit(im);
    raw.finish();for(auto n:verified)require(n==2);
    std::cout<<"8actualRAW full12bitCRC elapsed_ms="<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count()<<"\n";
  }
  std::cout<<"PASS bounded/no-drop/nonblocking/order/exception/drain\n";
}
