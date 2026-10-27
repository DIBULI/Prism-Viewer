#pragma once
#include "prism/usb/camera_control.hpp"
#include "communication/profile_capture.hpp"
#include <chrono>
#include <optional>
#include <stdexcept>

namespace prism_viewer {
struct NativeControlRequest {
  bool apply=false;
  std::optional<prism::CameraControlIdleStatus> idle;
  std::optional<prism::CameraControlStatus> live;
  std::array<prism::CameraRowGainConfig,4> configuration{};
};
// Capture-worker owned. Only send requests and recognize matching replies.
// Never read the transport, STOP, retry, skip image frames or compensate time.
class NativeCameraControl {
 public:
  using Clock=std::chrono::steady_clock;
  template<class Client> void begin(Client& c,const NativeControlRequest& request,
      Clock::time_point now=Clock::now()) {
    if(waiting_ || c.cameraControlPending())
      throw std::runtime_error("Previous row/gain response unconfirmed; no automatic retry");
    if(request.apply) {
      if(!request.live || request.idle)
        throw std::runtime_error("Capture changed; refresh row/gain state before applying");
      c.beginCameraControlSet(*request.live,request.configuration);
    } else c.beginCameraControlGet();
    waiting_=true;warned_=false;deadline_=now+std::chrono::seconds(5);
  }
  template<class Client> bool accept(Client& c,const prism::Frame& frame,prism::CameraControlStatus& status) {
    if(!waiting_)return false;
    try {if(!c.finishCameraControl(frame,status))return false;waiting_=false;return true;}
    catch(...) {waiting_=false;throw;}
  }
  bool expired(Clock::time_point now=Clock::now()) {
    if(!waiting_||warned_||now<deadline_)return false;
    warned_=true;return true;
  }
  bool waiting()const {return waiting_;}
  void reset(){waiting_=false;warned_=false;}
 private:
  bool waiting_=false,warned_=false;
  Clock::time_point deadline_{};
};
template<class Client,class Opened> void readProductExposure(Client& client,Opened& opened) {
  (void)captureProduct(opened.device_info.device_model);
  if(opened.device_info.device_model==prism::DeviceModel::Prism415M)
    opened.native_control_idle=client.cameraControlIdle();
  else {
    opened.exposure=client.cameraExposure();
    opened.exposure_limits=client.cameraExposureLimits();
  }
}
}
