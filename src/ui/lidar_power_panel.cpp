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
  connect(model_,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this]{clear();refresh();});
  clear();refresh();
}
void LidarPowerPanel::refresh(){
  model_->setEnabled(available_&&!busy_);
  for(auto* b:{query_,standby_,wake_})b->setEnabled(available_&&!busy_&&model_->currentData().toInt()>0);
}
void LidarPowerPanel::setAvailable(bool available,bool invalidate){
  available_=available;if(invalidate&&!busy_)clear();refresh();
}
void LidarPowerPanel::setBusy(bool busy){
  busy_=busy;if(busy)status_->setText(uiText("Waiting for hardware readback…","正在等待硬件回读……"));refresh();
}
void LidarPowerPanel::clear(){status_->setText(uiText("State unknown; read to confirm.","状态未知，请读取确认。"));}
void LidarPowerPanel::setError(const QString& error){status_->setText(uiText("State unknown; read before retrying: %1","状态未知，请读取确认后再重试：%1").arg(error));}
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
