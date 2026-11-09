#include "communication/opened_device.hpp"
#include "prism/usb/configuration.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace prism_viewer::communication;
static void check(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
struct FakeClient {
  unsigned configuration=0, native=0, legacy=0, gnss=0, network=0;
  bool forbid_product=true, reject_native=false;
  bool unsupported_native=false,unsupported_optional=false;
  bool cameraControlPending()const{return false;}
  prism::HelloInfo hello() { prism::HelloInfo h; h.version="1.2.1"; return h; }
  prism::DeviceVersions deviceVersions() { prism::DeviceVersions v; v.agent="1.2.1"; return v; }
  std::wstring serialNumber() { return L"legacy-test-serial"; }
  std::wstring path() { return L"test-path"; }
  void product() { check(!forbid_product,"maintenance open sent a product command"); }
  prism::DeviceConfiguration deviceConfiguration() { product(); ++configuration; return {}; }
  prism::CameraControlIdleStatus cameraControlIdle() {
    product(); ++native;
    if(reject_native) throw std::runtime_error("unconfirmed native state");
    if(unsupported_native)throw std::runtime_error("agent error: matched 415RSV0 control descriptor v4 required");
    return {};
  }
  prism::ExposureConfiguration cameraExposure() { product(); ++legacy; return {}; }
  prism::ExposureLimits cameraExposureLimits() { product(); ++legacy; return {}; }
  prism::GnssTimingStatus gnssTimingStatus() { product(); ++gnss;if(unsupported_optional)throw std::runtime_error("agent error: unknown message type"); return {}; }
  prism::NetworkInfo networkInfo() { product(); ++network;if(unsupported_optional)throw std::runtime_error("agent error: unknown message type"); return {}; }
};
static prism::Frame frame(unsigned version,unsigned model=0) {
  prism::Frame f; f.type=prism::FrameType::DeviceInfoResponse;
  f.payload.resize(prism::kDeviceInfoPayloadSize);
  f.payload[0]=uint8_t(version);
  f.payload[2]=uint8_t(prism::kDeviceInfoPayloadSize);
  f.payload[3]=uint8_t(prism::kDeviceInfoPayloadSize>>8);
  f.payload[4]=3; // USB3 and Board online, but no required camera/IMU.
  f.payload[8]=uint8_t(prism::UsbLinkSpeed::UsbSuperSpeed);
  f.payload[20]=uint8_t(prism::kOnboardImuRateHz);
  f.payload[21]=uint8_t(prism::kOnboardImuRateHz>>8);
  f.payload[22]=30;
  if(version==5) f.payload[255]=uint8_t(model);
  return f;
}
int main() {
  try {
    // Valid absent identity defaults to 413, including v5 Unknown; malformed
    // or unsupported identities are still rejected by the parser below.
    for(unsigned version:{3u,4u,5u}) {
      FakeClient client; client.forbid_product=false;
      const auto opened=readOpenedDeviceSnapshot(client,[&](auto&) {
        return parseCompatibleDeviceInfo(frame(version));
      });
      check(!opened.maintenance_only && opened.legacy_model_selected,
            "missing model did not use product default");
      check(opened.device_info.device_model==prism::DeviceModel::Prism413GSV0,
            "missing model default is not 413");
      check(client.configuration==1 && client.legacy==2 && client.native==0,
            "default 413 initialization changed");
    }
    for(unsigned version:{3u,4u}){
      FakeClient old;old.forbid_product=false;old.unsupported_optional=true;
      const auto opened=readOpenedDeviceSnapshot(old,[&](auto&){return parseCompatibleDeviceInfo(frame(version));},prism::DeviceModel::Prism413GSV0);
      check(!opened.maintenance_only&&opened.legacy_model_selected&&old.legacy==2&&!old.native,"SDK1.2.1 legacy model selection failed");
    }
    for(unsigned model:{1u,2u,3u}) {
      FakeClient client; client.forbid_product=false;
      auto opened=readOpenedDeviceSnapshot(client,[&](auto&) {
        return parseCompatibleDeviceInfo(frame(5,model));
      });
      const bool native=opened.device_info.device_model==prism::DeviceModel::Prism415RSV0;
      check(!opened.maintenance_only&&client.configuration==1&&client.network==1,"known open changed");
      check(client.native==(native?1u:0u)&&client.legacy==(native?0u:2u),"product exposure changed");
      check(client.gnss==(model==1?1u:0u),"product GNSS changed");
    }
    for(unsigned model:{1u,2u}){
      FakeClient old;old.forbid_product=false;old.unsupported_native=old.unsupported_optional=true;
      const auto opened=readOpenedDeviceSnapshot(old,[&](auto&){return parseCompatibleDeviceInfo(frame(5,model));});
      check(!opened.maintenance_only&&!opened.native_control_idle&&!opened.gnss_timing,"old optional feature rejected connection");
      check(old.legacy==(model==1?2u:0u),"415 native failure invoked legacy control");
    }
    // Corrupt/future packets must still fail. This is not a blanket error bypass.
    for(unsigned fault=0;fault<3;++fault) {
      FakeClient client;
      auto bad=frame(5);
      if(fault==0) bad.payload.pop_back();
      if(fault==1) bad.payload[255]=99;
      if(fault==2) bad.payload[0]=99;
      bool rejected=false;
      try { (void)readOpenedDeviceSnapshot(client,[&](auto&) {return parseCompatibleDeviceInfo(bad);}); }
      catch(const std::runtime_error&) {rejected=true;}
      check(rejected,"invalid DeviceInfo admitted");
    }
    FakeClient client;client.forbid_product=false;client.reject_native=true;
    bool rejected=false;
    try { (void)readOpenedDeviceSnapshot(client,[&](auto&) {return parseCompatibleDeviceInfo(frame(5,2));}); }
    catch(const std::runtime_error&) {rejected=true;}
    check(rejected&&client.native==1&&!client.legacy,"native failure bypassed/retried");
    std::cout<<"PASS legacy default-413 open; known routing and malformed/native-error protection\n";
  } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
