#include "ui/native_camera_control_panel.hpp"
#include "common/ui_text.hpp"
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QFrame>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QVBoxLayout>
#include <cmath>

namespace prism_viewer::ui {
using prism_viewer::common::uiText;
NativeCameraControlPanel::NativeCameraControlPanel(QWidget* parent):QWidget(parent) {
  auto* root=new QVBoxLayout(this);root->setContentsMargins(0,0,0,0);root->setSpacing(8);
  // Match the 41M3 exposure panel's hierarchy, two-column fields, camera
  // cards and shared action row. Sensor-specific units/protocol stay native.
  auto* group=new QGroupBox(uiText("Runtime Exposure and Gain","运行时曝光与增益"),this);
  group->setObjectName("nativeExposureGroup");root->addWidget(group);
  auto* layout=new QVBoxLayout(group);layout->setSpacing(8);layout->setAlignment(Qt::AlignTop);
  auto wrappedLabel=[](QWidget* parent) {
    auto* label=new QLabel(parent);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    // Wrapped telemetry must not impose a platform/font-dependent panel width.
    // Preserve QLabel's height-for-width policy so text remains fully visible.
    auto policy=label->sizePolicy();policy.setHorizontalPolicy(QSizePolicy::Ignored);
    label->setSizePolicy(policy);label->setMinimumWidth(0);
    return label;
  };
  auto* title=wrappedLabel(group);title->setText(uiText("External trigger · integer exposure rows · runtime settings",
                               "外触发 · 整数曝光行数 · 运行时设置"));layout->addWidget(title);
  status_=wrappedLabel(group);status_->setObjectName("nativeControlStatus");layout->addWidget(status_);
  auto section=[&](const QString& text){auto* label=wrappedLabel(group);label->setText(text);
    label->setStyleSheet("font-weight: 600; color: #344054;");layout->addWidget(label);return label;};
  section(uiText("Global settings · all cameras","全局设置 · 所有相机"))->setObjectName("nativeGlobalTitle");
  auto* global=new QWidget(group);auto* shared=new QGridLayout(global);
  shared->setContentsMargins(0,0,0,0);shared->setHorizontalSpacing(10);shared->setVerticalSpacing(4);
  shared->setColumnStretch(0,1);shared->setColumnStretch(1,1);
  auto globalSpin=[&](const char* name,int lo,int hi){auto* w=new QSpinBox(global);w->setObjectName(name);w->setRange(lo,hi);return w;};
  auto globalGain=[&](const char* name){auto* w=new QDoubleSpinBox(global);w->setObjectName(name);w->setDecimals(4);w->setRange(1,56.25);w->setSingleStep(.0625);w->setSuffix(QStringLiteral("×"));return w;};
  min_rows_=globalSpin("nativeMinRows",3,4096);max_rows_=globalSpin("nativeMaxRows",3,4096);
  min_gain_=globalGain("nativeMinGain");max_gain_=globalGain("nativeMaxGain");
  target_=globalSpin("nativeTarget",1,4095);deadband_=globalSpin("nativeDeadband",0,4094);
  ae_=new QCheckBox(uiText("Auto exposure","自动曝光"),global);ae_->setObjectName("nativeAe");
  agc_=new QCheckBox(uiText("Auto gain","自动增益"),global);agc_->setObjectName("nativeAgc");
  shared->addWidget(ae_,0,0);shared->addWidget(agc_,0,1);
  connect(ae_,&QCheckBox::toggled,this,[this](){updateEnabled();});
  connect(agc_,&QCheckBox::toggled,this,[this](){updateEnabled();});
  auto field=[&](QGridLayout* grid,int row,int col,const QString& name,QWidget* input){
    auto* label=wrappedLabel(input->parentWidget());label->setText(name);
    grid->addWidget(label,row,col,Qt::AlignBottom);grid->addWidget(input,row+1,col);
    input->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
  };
  field(shared,1,0,uiText("Target brightness (RAW12)","目标亮度（RAW12）"),target_);
  field(shared,1,1,uiText("Brightness deadband (RAW12)","亮度容差（RAW12）"),deadband_);
  field(shared,3,0,uiText("Minimum exposure (rows)","最小曝光（行）"),min_rows_);
  field(shared,3,1,uiText("Maximum exposure (rows)","最大曝光（行）"),max_rows_);
  field(shared,5,0,uiText("Minimum gain","最小增益（×）"),min_gain_);
  field(shared,5,1,uiText("Maximum gain","最大增益（×）"),max_gain_);
  layout->addWidget(global);
  section(uiText("Per-camera settings","各相机设置"))->setObjectName("nativeCameraTitle");
  for(unsigned i=0;i<4;++i) {
    auto* box=new QFrame(group);box->setFrameShape(QFrame::StyledPanel);
    box->setObjectName(QStringLiteral("nativeCamera%1Card").arg(i));
    auto* form=new QGridLayout(box);auto& c=cameras_[i];
    form->setContentsMargins(8,7,8,8);form->setHorizontalSpacing(8);form->setVerticalSpacing(4);
    form->setColumnStretch(0,1);form->setColumnStretch(1,1);
    auto* name=wrappedLabel(box);name->setText(uiText("Camera %1","相机 %1").arg(i));
    name->setStyleSheet("font-weight: 600;");form->addWidget(name,0,0,1,2);
    auto spin=[&](const char* name,int lo,int hi) {auto* w=new QSpinBox(box);w->setObjectName(QString::fromLatin1(name)+QString::number(i));w->setRange(lo,hi);return w;};
    auto gain=[&](const char* name) {auto* w=new QDoubleSpinBox(box);w->setObjectName(QString::fromLatin1(name)+QString::number(i));w->setDecimals(4);w->setRange(1,56.25);w->setSingleStep(.0625);w->setSuffix(QStringLiteral("×"));return w;};
    c.rows=spin("nativeRows",3,4096);c.gain=gain("nativeGain");
    field(form,1,0,uiText("Manual exposure (rows)","手动曝光（行）"),c.rows);
    field(form,1,1,uiText("Manual gain","手动增益（×）"),c.gain);
    c.observed=wrappedLabel(box);form->addWidget(c.observed,3,0,1,2);
    layout->addWidget(box);
  }
  auto* actions=new QHBoxLayout();actions->setSpacing(8);
  refresh_=new QPushButton(uiText("Refresh","刷新"),group);refresh_->setObjectName("nativeRefresh");actions->addWidget(refresh_,1);
  apply_=new QPushButton(uiText("Apply Settings","应用设置"),group);apply_->setObjectName("nativeApply");actions->addWidget(apply_,1);
  layout->addLayout(actions);
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
  const auto g=prism::cameraControlGlobals(v);
  min_rows_->setValue(g.min_rows);max_rows_->setValue(g.max_rows);
  min_gain_->setValue(g.min_gain_sixteenths/16.0);max_gain_->setValue(g.max_gain_sixteenths/16.0);
  target_->setValue(g.target_raw12);deadband_->setValue(g.deadband_raw12);
  ae_->setChecked(g.automatic_exposure);agc_->setChecked(g.automatic_gain);
  for(unsigned i=0;i<4;++i) {auto& w=cameras_[i];const auto& c=v[i];
    w.rows->setValue(c.manual_rows);w.gain->setValue(c.manual_gain_sixteenths/16.0);
  }
}
std::array<prism::CameraRowGainConfig,4> NativeCameraControlPanel::edited()const {
  std::array<prism::CameraRowGainConfig,4> v{};
  auto exactGain=[](double n){const auto q=std::lround(n*16);if(std::abs(n*16-q)>.00001)throw std::invalid_argument("Gain must be a multiple of 1/16x");return uint16_t(q);};
  for(unsigned i=0;i<4;++i){const auto& w=cameras_[i];auto& c=v[i];
    c.manual_rows=w.rows->value();c.manual_gain_sixteenths=exactGain(w.gain->value());
  }
  return prism::withCameraControlGlobals(v,{uint16_t(min_rows_->value()),uint16_t(max_rows_->value()),
    exactGain(min_gain_->value()),exactGain(max_gain_->value()),uint16_t(target_->value()),uint16_t(deadband_->value()),ae_->isChecked(),agc_->isChecked()});
}
void NativeCameraControlPanel::setAvailability(bool connected,bool active,bool locked) {
  if(connected_!=connected||active_!=active){idle_.reset();live_.reset();
    status_->setText(uiText("State changed; refresh before editing","状态已变化，请刷新后编辑"));
    for(auto& c:cameras_)c.observed->clear();}
  connected_=connected;active_=active;locked_=locked;updateEnabled();
}
void NativeCameraControlPanel::setBusy(bool v){busy_=v;updateEnabled();}
void NativeCameraControlPanel::setIdle(const prism::CameraControlIdleStatus& s) {
  if(!prism::sharedCameraControlGlobals(s.requested)){idle_.reset();live_.reset();setError(uiText("Inconsistent global settings; refresh matching firmware","全局参数不一致，请确认固件并刷新"));updateEnabled();return;}
  idle_=s;live_.reset();showConfiguration(s.requested);
  status_->setText(uiText("Idle configuration staged in RAM; sensor readback happens at START. Not saved to flash.",
                         "空闲参数仅暂存在内存；开始采集时写入并回读传感器。未固化。"));
  for(auto& c:cameras_)c.observed->setText(uiText("No captured exposure observation","尚无采集帧实测参数"));
  updateEnabled();
}
void NativeCameraControlPanel::setLive(const prism::CameraControlStatus& s) {
  if(!prism::sharedCameraControlGlobals(s.requested)){idle_.reset();live_.reset();setError(uiText("Inconsistent global settings; refresh matching firmware","全局参数不一致，请确认固件并刷新"));updateEnabled();return;}
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
  for(auto* w:{min_rows_,max_rows_,target_,deadband_})w->setEnabled(enabled&&valid);
  min_gain_->setEnabled(enabled&&valid);max_gain_->setEnabled(enabled&&valid);
  ae_->setEnabled(enabled&&valid);agc_->setEnabled(enabled&&valid);
  for(auto& c:cameras_){
    if(!c.rows)continue;
    c.rows->setEnabled(enabled&&valid&&!ae_->isChecked());c.gain->setEnabled(enabled&&valid&&!agc_->isChecked());
  }
}
}
