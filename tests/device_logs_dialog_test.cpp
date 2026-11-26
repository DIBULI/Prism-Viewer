#include "ui/device_logs_dialog.hpp"
#include "common/ui_text.hpp"
#include <QApplication>
#include <QDialog>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>
int main(int argc,char** argv){
  QApplication app(argc,argv);QTemporaryDir temp;
  for(bool chinese:{false,true})for(int mode=0;mode<3;++mode){
    prism_viewer::common::setChineseUi(chinese);
    const auto path=temp.filePath(QString::fromUtf8("设备 日志.zip"));
    {QFile before(path);if(!before.open(QIODevice::WriteOnly)||before.write("previous")!=8)return 1;}
    QTimer timeout,poll;timeout.setSingleShot(true);bool seen=false;
    QObject::connect(&timeout,&QTimer::timeout,&app,[]{std::exit(2);});
    QObject::connect(&poll,&QTimer::timeout,&app,[&]{
      auto* dialog=QApplication::activeModalWidget();if(!dialog)return;
      auto* button=dialog->findChild<QPushButton*>("deviceLogsClose");auto* label=dialog->findChild<QLabel*>("deviceLogsStatus");
      if(button&&button->text()==prism_viewer::common::uiText("Close","关闭")){
        seen=label&&!label->text().isEmpty();button->click();
      }
    });timeout.start(5000);poll.start(10);
    prism_viewer::ui::showDeviceLogsDialog(nullptr,path,[mode](const prism::DeviceLogProgress& p,const prism::DeviceLogCancel&){
      if(mode==1)throw std::runtime_error("fixture unavailable");
      if(mode==2)throw prism::DeviceLogCancelled();
      for(unsigned i=1;i<=4;++i)p(i,4);
      return prism::DeviceLogArchive{{'P','K','f','i','x','t','u','r','e'},true};
    });
    QFile result(path);if(!seen||!result.open(QIODevice::ReadOnly)||result.readAll()!=(mode?"previous":"PKfixture"))return 3;
  }
  std::cout<<"PASS bilingual device log export: asynchronous completion, Unicode save, partial notice, failure/cancel preserve destination\n";
}
