#include "communication/native_camera_control.hpp"
#include "communication/device_session.hpp"
#include "prism/usb/camera_control_runtime_api.hpp"
#include <cassert>
#include <iostream>
struct Fake {
 bool pending=false,invalid=false,reject_idle=false;
 int gets=0,sets=0,idles=0,old=0;
 prism::CameraControlStatus expected;
 bool cameraControlPending()const{return pending;}
 void beginCameraControlGet(){++gets;pending=true;}
 void beginCameraControlSet(const prism::CameraControlStatus& s,const std::array<prism::CameraRowGainConfig,4>&){++sets;expected=s;pending=true;}
 bool finishCameraControl(const prism::Frame& f,prism::CameraControlStatus& s){
  if(f.type!=prism::FrameType::CameraControlResponse||f.sequence!=17)return false;
  if(invalid)throw std::runtime_error("invalid reply remains pending");
  pending=false;s=expected;return true;
 }
 prism::CameraControlIdleStatus cameraControlIdle(){++idles;if(reject_idle)throw std::runtime_error("native unavailable");return {};}
 prism::ExposureConfiguration cameraExposure(){++old;return {};}
 prism::ExposureLimits cameraExposureLimits(){++old;return {};}
};
template<class F>void rejects(F f){bool yes=false;try{f();}catch(const std::exception&){yes=true;}assert(yes);}
int main(){
 using prism_viewer::NativeCameraControl;using prism_viewer::NativeControlRequest;
 Fake c;NativeCameraControl n;const auto t=NativeCameraControl::Clock::time_point{};
 NativeControlRequest q;n.begin(c,q,t);assert(c.gets==1&&n.waiting());
 prism::CameraControlStatus s;prism::Frame f;f.sequence=17;
 for(auto type:{prism::FrameType::VideoChunk,prism::FrameType::ImuSample,prism::FrameType::Heartbeat}){f.type=type;assert(!n.accept(c,f,s));}
 f.type=prism::FrameType::CameraControlResponse;f.sequence=18;assert(!n.accept(c,f,s));
 assert(!n.expired(t+std::chrono::seconds(4)));assert(n.expired(t+std::chrono::seconds(5)));assert(!n.expired(t+std::chrono::seconds(6)));
 rejects([&](){n.begin(c,q);});assert(c.gets==1);f.sequence=17;assert(n.accept(c,f,s));
 q.apply=true;q.idle=prism::CameraControlIdleStatus{};rejects([&](){n.begin(c,q);});assert(!c.sets);
 q.idle.reset();s.capture_epoch=91;s.config_revision=12;s.running=true;q.live=s;
 n.begin(c,q,t);assert(c.sets==1&&c.expected.capture_epoch==91&&c.expected.config_revision==12);
 c.invalid=true;rejects([&](){n.accept(c,f,s);});assert(!n.waiting()&&c.pending);
 n.reset();rejects([&](){n.begin(c,q);});assert(c.sets==1);
 for(auto model:{prism::DeviceModel::Prism415M,prism::DeviceModel::Prism41M3,prism::DeviceModel::Prism1315}){
  Fake client;prism_viewer::communication::OpenedDevice o;o.device_info.device_model=model;
  prism_viewer::readProductExposure(client,o);
  assert(client.idles==(model==prism::DeviceModel::Prism415M?1:0));assert(client.old==(model==prism::DeviceModel::Prism415M?0:2));
 }
 Fake fail;fail.reject_idle=true;prism_viewer::communication::OpenedDevice o;o.device_info.device_model=prism::DeviceModel::Prism415M;
 rejects([&](){prism_viewer::readProductExposure(fail,o);});assert(!fail.old);
 assert(!prism::validCameraControlRuntimeApi(nullptr));
 prism::CameraControlRuntimeApi api{};assert(!prism::validCameraControlRuntimeApi(&api));
 std::cout<<"PASS native async no-read/no-stop/no-retry/frame-preservation and product routing\n";
}
