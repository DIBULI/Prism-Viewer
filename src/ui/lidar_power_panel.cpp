#include "lidar_power_panel.hpp"
#include "common/ui_text.hpp"
#include <QtWidgets/QComboBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QGridLayout>
namespace prism_viewer::ui {
using common::uiText;
LidarPowerPanel::LidarPowerPanel(QWidget* parent):QWidget(parent) {
  auto* layout=new QGridLayout(this);
  layout->setContentsMargins(0,0,0,0);
  layout->setSpacing(8);
  auto* hint=new QLabel(uiText("Stop all capture before changing hardware state. Wake does not start capture or recording.",
    "请先停止所有采集再切换硬件状态。唤醒不会开始采集或录制。"),this);
  hint->setWordWrap(true);layout->addWidget(hint,0,0,1,2);
  model_=new QComboBox(this);model_->setObjectName("lidarPowerModel");
  model_->addItem(uiText("Select model", "选择型号"),0);
  model_->addItem("Livox Mid-360",1);model_->addItem("Livox Mid-360S",2);model_->addItem("Hesai PandarXT-32",3);
  layout->addWidget(model_,1,0,1,2);
  query_=new QPushButton(uiText("Read state","读取状态"),this);
  standby_=new QPushButton(uiText("Standby","待机"),this);
  wake_=new QPushButton(uiText("Wake","唤醒"),this);
  query_->setObjectName("lidarPowerQuery");standby_->setObjectName("lidarPowerStandby");wake_->setObjectName("lidarPowerWake");
  int action=0;
  for(auto* button:{query_,wake_,standby_}) {
    const int op=action++;
    connect(button,&QPushButton::clicked,this,[this,op]{
      if(available_&&!busy_&&model_->currentData().toInt()>0&&on_action)
        on_action(static_cast<prism::LidarModel>(model_->currentData().toInt()),op);
    });
  }
  layout->addWidget(query_,2,0,1,2);
  layout->addWidget(standby_,3,0);layout->addWidget(wake_,3,1);
  layout->setColumnStretch(0,1);layout->setColumnStretch(1,1);
  status_=new QLabel(this);status_->setObjectName("lidarPowerState");status_->setWordWrap(true);layout->addWidget(status_,4,0,1,2);
  speed_panel_=new QWidget(this);
  auto* speeds=new QGridLayout(speed_panel_);speeds->setContentsMargins(0,0,0,0);
  auto* speed_hint=new QLabel(uiText("MID-360S motor speed. Read first. Low speed is not standby; not automatically saved or reapplied after restart.",
    "MID-360S 转速：先读取确认。低速不是待机；不自动保存或在重启后重应用。"),speed_panel_);
  speed_hint->setWordWrap(true);speeds->addWidget(speed_hint,0,0,1,2);
  speed_query_=new QPushButton(uiText("Read speed","读取转速"),speed_panel_);
  speed_normal_=new QPushButton(uiText("Normal (high)","正常转速（高）"),speed_panel_);
  speed_low_=new QPushButton(uiText("Low speed","低转速"),speed_panel_);
  speed_query_->setObjectName("lidarSpeedQuery");speed_normal_->setObjectName("lidarSpeedNormal");speed_low_->setObjectName("lidarSpeedLow");
  int speed_action=0;
  for(auto* button:{speed_query_,speed_normal_,speed_low_}){
    const int op=speed_action++;
    connect(button,&QPushButton::clicked,this,[this,op]{
      if(available_&&!busy_&&model_->currentData().toInt()==2&&(!op||speed_known_)&&on_speed_action)
        on_speed_action(prism::LidarModel::Mid360S,op);
    });
  }
  speeds->addWidget(speed_query_,1,0,1,2);speeds->addWidget(speed_normal_,2,0);speeds->addWidget(speed_low_,2,1);
  speed_status_=new QLabel(this);speed_status_->setObjectName("lidarSpeedState");speed_status_->setWordWrap(true);
  speeds->addWidget(speed_status_,3,0,1,2);speeds->setColumnStretch(0,1);speeds->setColumnStretch(1,1);
  layout->addWidget(speed_panel_,5,0,1,2);
  connect(model_,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this]{clear();refresh();});
  clear();refresh();
}
void LidarPowerPanel::refresh(){
  model_->setEnabled(available_&&!busy_);
  for(auto* b:{query_,standby_,wake_})b->setEnabled(available_&&!busy_&&model_->currentData().toInt()>0);
  const bool speed_model=model_->currentData().toInt()==2;
  speed_panel_->setVisible(speed_model);
  speed_query_->setEnabled(available_&&!busy_&&speed_model);
  for(auto* b:{speed_normal_,speed_low_})b->setEnabled(available_&&!busy_&&speed_model&&speed_known_);
}
void LidarPowerPanel::setAvailable(bool available,bool invalidate){
  available_=available;if(invalidate&&!busy_)clear();refresh();
}
void LidarPowerPanel::setBusy(bool busy){
  busy_=busy;if(busy){clear();status_->setText(uiText("Waiting for hardware readback…","正在等待硬件回读……"));}refresh();
}
void LidarPowerPanel::clear(){status_->setText(uiText("State unknown; read to confirm.","状态未知，请读取确认。"));speed_known_=false;speed_status_->setText(uiText("Motor speed not confirmed","尚未确认转速"));refresh();}
void LidarPowerPanel::setError(const QString& error){clear();status_->setText(uiText("State unknown; read before retrying: %1","状态未知，请读取确认后再重试：%1").arg(error));}
void LidarPowerPanel::setSpeedResult(const prism::LidarSpeedStatus& s){
  if(model_->currentData().toInt()!=2||s.model!=prism::LidarModel::Mid360S||s.device_type!=35||
     (s.mode!=prism::LidarSpeedMode::Normal&&s.mode!=prism::LidarSpeedMode::Low)){clear();return;}
  speed_known_=true;
  speed_status_->setText(uiText("Last readback: %1","最近回读：%1").arg(s.mode==prism::LidarSpeedMode::Normal?
      uiText("Normal (high)","正常转速（高）"):uiText("Low speed","低转速")));
  refresh();
}
void LidarPowerPanel::setResult(const prism::LidarPowerStatus& s){
  if(int(s.model)!=model_->currentData().toInt()){clear();return;}
  QString text;
  switch(s.state){
    case prism::LidarPowerState::Running:text=uiText("Scanning","扫描中");break;
    case prism::LidarPowerState::Standby:text=uiText("Standby","待机");break;
    case prism::LidarPowerState::Transitioning:text=uiText("Transitioning","切换中");break;
    case prism::LidarPowerState::Error:text=uiText("Error","故障");break;
    default:text=uiText("Unknown","未知");break;
  }
  status_->setText(uiText("Hardware readback: %1","硬件回读：%1").arg(text));
}
}
