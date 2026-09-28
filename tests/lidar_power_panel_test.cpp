#include "ui/lidar_power_panel.hpp"
#include "common/ui_text.hpp"
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QLabel>
#include <cstdlib>
#include <iostream>
void check(bool ok,const char* message){if(!ok){std::cerr<<message<<'\n';std::exit(1);}}
int main(int argc,char** argv){
 QApplication app(argc,argv);
 for(bool chinese:{false,true}){
  prism_viewer::common::setChineseUi(chinese);
  prism_viewer::ui::LidarPowerPanel p;
  auto* model=p.findChild<QComboBox*>("lidarPowerModel");
  auto* standby=p.findChild<QPushButton*>("lidarPowerStandby");
  auto* wake=p.findChild<QPushButton*>("lidarPowerWake");
  auto* query=p.findChild<QPushButton*>("lidarPowerQuery");
  auto* state=p.findChild<QLabel*>("lidarPowerState");
  check(model&&standby&&wake&&query&&state,"missing controls");
  check(!standby->isEnabled(),"closed device accepted power command");
  p.setAvailable(true);check(!wake->isEnabled(),"unselected model accepted");
  model->setCurrentIndex(2);check(standby->isEnabled(),"idle power controls disabled");
  int command=-1;p.on_action=[&](prism::LidarModel m,int op){check(m==prism::LidarModel::Mid360S,"wrong model");command=op;};
  standby->click();check(command==2,"standby operation wrong");
  wake->click();check(command==1,"wake operation wrong");query->click();check(command==0,"query operation wrong");
  p.setBusy(true);check(!query->isEnabled()&&!model->isEnabled(),"busy controls unlocked");
  p.setBusy(false);prism::LidarPowerStatus s;s.model=prism::LidarModel::Mid360S;s.state=prism::LidarPowerState::Standby;p.setResult(s);
  check(state->text().contains(chinese?QStringLiteral("待机"):QStringLiteral("Standby")),"readback missing");
  p.setAvailable(false,true);check(!wake->isEnabled(),"capture controls unlocked");
  check(!state->text().contains(chinese?QStringLiteral("待机"):QStringLiteral("Standby")),"stale success retained");
  p.setAvailable(true);p.setError("timeout");check(state->text().contains("timeout"),"error missing");
 }
}
