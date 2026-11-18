#include "communication/prism_runtime.hpp"
#include "communication/profile_capture.hpp"
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
  for(auto product : {prism_viewer::CaptureProduct::Strict415RSV0,
                      prism_viewer::CaptureProduct::Mixed21312V0}) {
    calls=0;
    prism_runtime::ImuStream independent(client,handler);
    prism_runtime::AggregateImuObserver aggregate(handler);
    require(prism_viewer::usesAggregateImu(product));
    f.type=prism::FrameType::ImuSample;
    for(unsigned i=0;i<1000;++i){
      for(unsigned k=0;k<4;++k)f.payload[4+k]=uint8_t(i>>(8*k));
      for(unsigned k=0;k<8;++k)f.payload[8+k]=uint8_t(uint64_t(1000+i)>>(8*k));
      require(prism_viewer::handleCaptureImu(product,f,aggregate,independent));
    }
    require(calls==1000&&!independent.active()&&!client.isOpen());
    auto bad=f;bad.payload.pop_back();bool rejected=false;
    try{prism_viewer::handleCaptureImu(product,bad,aggregate,independent);}
    catch(const std::runtime_error&){rejected=true;}
    require(rejected&&calls==1000);
    f.type=prism::FrameType::Heartbeat;
    require(!prism_viewer::handleCaptureImu(product,f,aggregate,independent));
  }
  // 413 keeps its separate lifecycle; it must never borrow the aggregate path.
  calls=0;f.type=prism::FrameType::ImuSample;
  prism_runtime::ImuStream independent(client,handler);
  prism_runtime::AggregateImuObserver aggregate(handler);
  require(!prism_viewer::usesAggregateImu(prism_viewer::CaptureProduct::Legacy413GSV0));
  require(prism_viewer::handleCaptureImu(prism_viewer::CaptureProduct::Legacy413GSV0,
                                      f,aggregate,independent)&&calls==0);
  std::cout<<"PASS inactive negative; 415+21312 each1000 callbacks; 413 separate lifecycle; malformed/nonIMU; no control writes\n";
}
