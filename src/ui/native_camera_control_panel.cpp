#include "ui/native_camera_control_panel.hpp"
#include "common/ui_text.hpp"
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QVBoxLayout>
#include <cmath>

namespace prism_viewer::ui {
using prism_viewer::common::uiText;
NativeCameraControlPanel::NativeCameraControlPanel(QWidget* parent):QWidget(parent) {
  auto* layout=new QVBoxLayout(this);
  auto* title=new QLabel(uiText("External trigger · integer exposure rows · runtime settings",
                               "外触发 · 整数曝光行数 · 运行时设置"),this);title->setWordWrap(true);layout->addWidget(title);
  status_=new QLabel(this);status_->setObjectName("nativeControlStatus");status_->setWordWrap(true);layout->addWidget(status_);
  for(unsigned i=0;i<4;++i) {
    auto* box=new QGroupBox(QStringLiteral("cam%1").arg(i),this);auto* form=new QFormLayout(box);auto& c=cameras_[i];
    auto spin=[&](const char* name,int lo,int hi) {auto* w=new QSpinBox(box);w->setObjectName(QString::fromLatin1(name)+QString::number(i));w->setRange(lo,hi);return w;};
    auto gain=[&](const char* name) {auto* w=new QDoubleSpinBox(box);w->setObjectName(QString::fromLatin1(name)+QString::number(i));w->setDecimals(4);w->setRange(1,56.25);w->setSingleStep(.0625);w->setSuffix(" x");return w;};
    c.ae=new QCheckBox(uiText("Auto exposure","自动曝光"),box);c.ae->setObjectName("nativeAe"+QString::number(i));
    c.agc=new QCheckBox(uiText("Auto gain","自动增益"),box);c.agc->setObjectName("nativeAgc"+QString::number(i));
    c.rows=spin("nativeRows",3,4096);c.min_rows=spin("nativeMinRows",3,4096);c.max_rows=spin("nativeMaxRows",3,4096);
    c.gain=gain("nativeGain");c.min_gain=gain("nativeMinGain");c.max_gain=gain("nativeMaxGain");
    c.target=spin("nativeTarget",1,4095);c.deadband=spin("nativeDeadband",0,4094);
    form->addRow(c.ae);form->addRow(uiText("Manual rows","手动曝光（行）"),c.rows);
    form->addRow(uiText("Minimum rows","最小曝光（行）"),c.min_rows);form->addRow(uiText("Maximum rows","最大曝光（行）"),c.max_rows);
    form->addRow(c.agc);form->addRow(uiText("Manual gain","手动增益"),c.gain);
    form->addRow(uiText("Minimum gain","最小增益"),c.min_gain);form->addRow(uiText("Maximum gain","最大增益"),c.max_gain);
    form->addRow(uiText("RAW12 target","RAW12 目标亮度"),c.target);form->addRow(uiText("Target deadband","目标亮度容差"),c.deadband);
    c.observed=new QLabel(box);c.observed->setWordWrap(true);form->addRow(c.observed);
    connect(c.ae,&QCheckBox::toggled,this,[this](){updateEnabled();});
    connect(c.agc,&QCheckBox::toggled,this,[this](){updateEnabled();});layout->addWidget(box);
  }
  refresh_=new QPushButton(uiText("Refresh","刷新"),this);refresh_->setObjectName("nativeRefresh");layout->addWidget(refresh_);
  apply_=new QPushButton(uiText("Submit settings","提交设置"),this);apply_->setObjectName("nativeApply");layout->addWidget(apply_);
  connect(refresh_,&QPushButton::clicked,this,[this](){if(on_request)on_request(NativeControlRequest{});});
  connect(apply_,&QPushButton::clicked,this,[this](){
    try {
      NativeControlRequest r;r.apply=true;r.configuration=edited();
      for(const auto& c:r.configuration)if(!prism::validCameraRowGainConfig(c))
        throw std::invalid_argument("Invalid rows/gain bounds or RAW12 target/deadband");
      if(active_) {if(!live_)throw std::logic_error("Refresh capture state first");r.live=live_;}
      else {if(!idle_)throw std::logic_error("Refresh idle state first");r.idle=idle_;}
      if(on_request)on_request(r);
    }catch(const std::exception& e){setError(QString::fromUtf8(e.what()));}
  });
  clear();
}
void NativeCameraControlPanel::showConfiguration(const std::array<prism::CameraRowGainConfig,4>& v) {
  for(unsigned i=0;i<4;++i) {auto& w=cameras_[i];const auto& c=v[i];
    w.ae->setChecked(c.automatic_exposure);w.agc->setChecked(c.automatic_gain);
    w.rows->setValue(c.manual_rows);w.min_rows->setValue(c.min_rows);w.max_rows->setValue(c.max_rows);
    w.gain->setValue(c.manual_gain_sixteenths/16.0);w.min_gain->setValue(c.min_gain_sixteenths/16.0);w.max_gain->setValue(c.max_gain_sixteenths/16.0);
    w.target->setValue(c.target_raw12);w.deadband->setValue(c.deadband_raw12);
  }
}
std::array<prism::CameraRowGainConfig,4> NativeCameraControlPanel::edited()const {
  std::array<prism::CameraRowGainConfig,4> v{};
  auto exactGain=[](double n){const auto q=std::lround(n*16);if(std::abs(n*16-q)>.00001)throw std::invalid_argument("Gain must be a multiple of 1/16x");return uint16_t(q);};
  for(unsigned i=0;i<4;++i){const auto& w=cameras_[i];auto& c=v[i];
    c.automatic_exposure=w.ae->isChecked();c.automatic_gain=w.agc->isChecked();
    c.manual_rows=w.rows->value();c.min_rows=w.min_rows->value();c.max_rows=w.max_rows->value();
    c.manual_gain_sixteenths=exactGain(w.gain->value());c.min_gain_sixteenths=exactGain(w.min_gain->value());c.max_gain_sixteenths=exactGain(w.max_gain->value());
    c.target_raw12=w.target->value();c.deadband_raw12=w.deadband->value();
  }return v;
}
void NativeCameraControlPanel::setAvailability(bool connected,bool active,bool locked) {
  if(connected_!=connected||active_!=active){idle_.reset();live_.reset();
    status_->setText(uiText("State changed; refresh before editing","状态已变化，请刷新后编辑"));
    for(auto& c:cameras_)c.observed->clear();}
  connected_=connected;active_=active;locked_=locked;updateEnabled();
}
void NativeCameraControlPanel::setBusy(bool v){busy_=v;updateEnabled();}
void NativeCameraControlPanel::setIdle(const prism::CameraControlIdleStatus& s) {
  idle_=s;live_.reset();showConfiguration(s.requested);
  status_->setText(uiText("Idle configuration staged in RAM; sensor readback happens at START. Not saved to flash.",
                         "空闲参数仅暂存在内存；开始采集时写入并回读传感器。未固化。"));
  for(auto& c:cameras_)c.observed->setText(uiText("No captured exposure observation","尚无采集帧实测参数"));
  updateEnabled();
}
void NativeCameraControlPanel::setLive(const prism::CameraControlStatus& s) {
  live_=s;idle_.reset();showConfiguration(s.requested);
  status_->setText(uiText("Accepted configuration revision %1; actual image exposure comes from that image's metadata. Refresh does not pause capture.",
                         "已接收配置版本 %1；实际图像曝光以该帧元数据为准。刷新不暂停采集。").arg(qulonglong(s.config_revision)));
  if(s.control_error)status_->setText(uiText("Controller error %1; no automatic retry","控制器错误 %1；不自动重试").arg(s.control_error));
  for(unsigned i=0;i<4;++i){const auto& o=s.observed[i];cameras_[i].observed->setText(o.known?
    uiText("Query snapshot: frame %1, %2 rows, %3x (observed revision %4; %5)","查询快照：帧 %1，%2 行，%3x（观测版本 %4；%5）")
      .arg(o.source_frame_id).arg(o.rows).arg(o.gain_sixteenths/16.0).arg(qulonglong(o.config_revision))
      .arg(o.fresh_at_snapshot?uiText("fresh at query","查询时有效"):uiText("historical","历史值")):
    uiText("No known frame observation","尚无已知帧观测值"));}
  updateEnabled();
}
void NativeCameraControlPanel::setError(const QString& e){status_->setText(e);}
void NativeCameraControlPanel::clear(){idle_.reset();live_.reset();connected_=active_=locked_=busy_=false;status_->setText(uiText("Open a matching 415M device","请连接匹配新版接口的 415M"));for(auto& c:cameras_)c.observed->clear();updateEnabled();}
void NativeCameraControlPanel::updateEnabled(){
  const bool enabled=connected_&&!locked_&&!busy_;
  const bool valid=active_?(live_&&live_->running&&!live_->control_error&&!live_->command_in_flight):bool(idle_);
  if(refresh_)refresh_->setEnabled(enabled);
  if(apply_)apply_->setEnabled(enabled&&valid);
  for(auto& c:cameras_){
    if(!c.ae)continue;
    c.ae->setEnabled(enabled&&valid);c.agc->setEnabled(enabled&&valid);
    c.rows->setEnabled(enabled&&valid&&!c.ae->isChecked());c.gain->setEnabled(enabled&&valid&&!c.agc->isChecked());
    for(auto* w:{c.min_rows,c.max_rows,c.target,c.deadband})w->setEnabled(enabled&&valid);
    c.min_gain->setEnabled(enabled&&valid);c.max_gain->setEnabled(enabled&&valid);
  }
}
}
