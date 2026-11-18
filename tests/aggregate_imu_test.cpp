#include "communication/prism_runtime.hpp"
#include "communication/profile_capture.hpp"
#include <iostream>
#include <stdexcept>
static void require(bool yes){if(!yes)throw std::runtime_error("aggregate IMU regression");}
int main(){
  prism::Frame f;f.type=prism::FrameType::ImuSample;f.payload.assign(44,0);
  unsigned calls=0;
  auto handler=[&](const prism::ImuSample& s){require(s.sensor_id==calls%2&&s.sample_id==calls&&s.timestamp_us==1000+calls);++calls;};
  // Actual old Linux SDK reproduces consuming a valid sample without callback.
  // No enumeration/open/control writes: client remains closed throughout.
  prism_runtime::Client client;
  {prism_runtime::ImuStream old(client,handler);require(old.handleFrame(f)&&calls==0&&!old.active());}
  for(auto product : {prism_viewer::CaptureProduct::Legacy413GSV0,
                      prism_viewer::CaptureProduct::Strict415RSV0,
                      prism_viewer::CaptureProduct::Mixed21312V0}) {
    calls=0;f.type=prism::FrameType::ImuSample;
    require(prism_viewer::captureOptions(product)<=7);
    // The production sample observer is identical for all product lifecycles.
    prism_runtime::ImuSampleObserver observer(handler);
    for(unsigned i=0;i<1000;++i){
      f.payload[0]=uint8_t(i%2);
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
  std::cout<<"PASS inactive negative; unified observer 1000 callbacks/model, both sensor IDs; malformed/nonIMU; no control writes\n";
}
