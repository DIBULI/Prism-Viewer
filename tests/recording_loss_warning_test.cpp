#include "ui/recording_loss_warning.hpp"
#include <QApplication>
#include <QLabel>
#include <iostream>
#include <stdexcept>
using prism_viewer::ui::RecordingCameraLoss;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char**argv){
  QApplication app(argc,argv);
  try{
    for(bool chinese:{false,true}){
      prism_viewer::common::setChineseUi(chinese);
      check(RecordingCameraLoss{}.warning().isEmpty(),"zero loss should be quiet");
      RecordingCameraLoss loss{{1,2,3,4},2,1};
      const auto text=loss.warning();check(text.contains("10")&&text.contains("1/2/3/4"),"missing per-camera and total counts");
      check(text.contains(chinese?QString::fromUtf8("警告"):QStringLiteral("WARNING")),"missing localized warning");
      QLabel banner;banner.setStyleSheet(prism_viewer::ui::recordingLossStyle());banner.setText(text);banner.show();app.processEvents();
      check(banner.isVisible()&&banner.text()==text&&banner.styleSheet().contains("#fffaeb"),"visible warning banner");
      check(!RecordingCameraLoss{{},1,0}.warning().isEmpty(),"invalid-frame loss hidden");
      check(!RecordingCameraLoss{{},0,1}.warning().isEmpty(),"partial-frame loss hidden");
    }
    std::cout<<"PASS live/final recording loss warning, total/per-camera counts, bilingual banner\n";
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
