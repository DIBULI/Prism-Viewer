#include "communication/prism_runtime.hpp"
#include <iostream>
#include <stdexcept>
static void require(bool yes){if(!yes)throw std::runtime_error("aggregate IMU regression");}
int main(){
  prism::Frame f;f.type=prism::FrameType::ImuSample;f.payload.assign(44,0);
  unsigned calls=0;
  auto handler=[&](const prism::ImuSample& s){require(s.sensor_id==0&&s.sample_id==calls&&s.timestamp_us==1000+calls);++calls;};
  // Actual old Linux SDK reproduces consuming a valid sample without callback.
  // No enumeration/open/control writes: client remains closed throughout.
  prism_runtime::Client client;
  {prism_runtime::ImuStream old(client,handler);require(old.handleFrame(f)&&calls==0&&!old.active());}
  {
    prism_runtime::AggregateImuObserver observer(handler);
    for(unsigned i=0;i<1000;++i){
      for(unsigned k=0;k<4;++k)f.payload[4+k]=uint8_t(i>>(8*k));
      for(unsigned k=0;k<8;++k)f.payload[8+k]=uint8_t(uint64_t(1000+i)>>(8*k));
      require(observer.handleFrame(f));
    }
    require(calls==1000&&!client.isOpen());
    auto bad=f;bad.payload.pop_back();bool rejected=false;
    try{observer.handleFrame(bad);}catch(const std::runtime_error&){rejected=true;}
    require(rejected&&calls==1000);
    f.type=prism::FrameType::Heartbeat;require(!observer.handleFrame(f)&&calls==1000);
  }
  require(!client.isOpen());
  std::cout<<"PASS old-inactive negative,1000aggregate callbacks,malformed/nonIMU,no control owner\n";
}
