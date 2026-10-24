#include "ui/native_camera_control_panel.hpp"
#include "common/ui_text.hpp"
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QFrame>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QStyleFactory>
#include <cassert>
#include <iostream>
int main(int argc,char** argv){
 QApplication app(argc,argv);prism_viewer::ui::NativeCameraControlPanel p;
 auto* apply=p.findChild<QPushButton*>("nativeApply");auto* refresh=p.findChild<QPushButton*>("nativeRefresh");
 auto* rows=p.findChild<QSpinBox*>("nativeRows2");auto* gain=p.findChild<QDoubleSpinBox*>("nativeGain2");
 auto* ae=p.findChild<QCheckBox*>("nativeAe");auto* agc=p.findChild<QCheckBox*>("nativeAgc");
 assert(apply&&refresh&&rows&&gain&&ae&&agc);assert(!apply->isEnabled());
 prism::CameraControlIdleStatus idle;idle.idle_epoch=7;idle.staged_revision=19;
 for(auto& c:idle.requested)c.manual_rows=1024;
 p.setAvailability(true,false,false);p.setIdle(idle);assert(apply->isEnabled()&&rows->value()==1024);
 unsigned calls=0;prism_viewer::NativeControlRequest last;
 p.on_request=[&](const auto& r){++calls;last=r;};
 rows->setValue(2048);gain->setValue(2.0625);ae->setChecked(true);agc->setChecked(true);assert(!rows->isEnabled()&&!gain->isEnabled());
 apply->click();assert(calls==1&&last.idle&&last.idle->staged_revision==19&&!last.live);
 assert(last.configuration[2].manual_rows==2048&&last.configuration[2].manual_gain_sixteenths==33&&last.configuration[2].automatic_exposure&&last.configuration[2].automatic_gain);
 assert(!p.findChild<QCheckBox*>("nativeAe2")&&!p.findChild<QCheckBox*>("nativeAgc2"));
 for(unsigned c=0;c<4;++c){assert(last.configuration[c].automatic_exposure&&last.configuration[c].automatic_gain);
   assert(!p.findChild<QSpinBox*>(QStringLiteral("nativeRows%1").arg(c))->isEnabled());}
 ae->setChecked(false);agc->setChecked(false);gain->setValue(1.03);apply->click();assert(calls==1);
 gain->setValue(1);p.findChild<QSpinBox*>("nativeMinRows")->setValue(3000);apply->click();assert(calls==1);
 p.setBusy(true);assert(!apply->isEnabled()&&!refresh->isEnabled());p.setBusy(false);
 p.setAvailability(true,true,false);assert(!apply->isEnabled()&&refresh->isEnabled());refresh->click();assert(calls==2&&!last.apply);
 prism::CameraControlStatus s;s.running=true;s.capture_epoch=77;s.config_revision=89;s.requested=idle.requested;
 s.observed[2].known=true;s.observed[2].fresh_at_snapshot=true;s.observed[2].rows=900;s.observed[2].gain_sixteenths=16;s.observed[2].source_frame_id=333;s.observed[2].config_revision=88;
 p.setLive(s);assert(rows->value()==1024&&apply->isEnabled());apply->click();assert(calls==3&&last.live&&last.live->capture_epoch==77&&last.live->config_revision==89&&!last.idle);
 p.setAvailability(true,false,false);assert(!apply->isEnabled());p.setIdle(idle);assert(apply->isEnabled());
 p.setAvailability(true,false,true);assert(!apply->isEnabled()&&!refresh->isEnabled());
 p.setAvailability(false,false,false);assert(!apply->isEnabled());
 p.setAvailability(true,false,false);p.setIdle(idle);p.resize(360,1800);p.show();app.processEvents();
 assert(!p.findChild<QSpinBox*>("nativeMinRows2"));
 auto* globalTarget=p.findChild<QSpinBox*>("nativeTarget");
 assert(globalTarget&&p.findChildren<QSpinBox*>("nativeTarget").size()==1);
 globalTarget->setValue(1600);p.findChild<QSpinBox*>("nativeMinRows")->setValue(10);
 p.findChild<QSpinBox*>("nativeMaxRows")->setValue(3000);
 p.findChild<QDoubleSpinBox*>("nativeMaxGain")->setValue(8);
 const auto prior=calls;apply->click();assert(calls==prior+1);
 for(const auto& c:last.configuration)assert(c.min_rows==10&&c.max_rows==3000&&c.max_gain_sixteenths==128&&c.target_raw12==1600&&c.manual_rows==1024);
 // Reject a global bound excluding ANY lane, not only the last-edited lane.
 rows->setValue(2048);p.findChild<QSpinBox*>("nativeMinRows")->setValue(1500);
 apply->click();assert(calls==prior+1);
 auto mixed=idle;mixed.requested[3].target_raw12=1555;p.setIdle(mixed);assert(!apply->isEnabled());
 p.setIdle(idle);assert(apply->isEnabled());
 assert(p.minimumSizeHint().width()<=360);
 app.processEvents();
 assert(p.findChildren<QGroupBox*>().size()==1);
 auto* card0=p.findChild<QFrame*>("nativeCamera0Card");auto* card1=p.findChild<QFrame*>("nativeCamera1Card");
 assert(card0&&card1&&card0->frameShape()==QFrame::StyledPanel);
 assert(card1->geometry().top()-card0->geometry().bottom()<=12);
 assert(rows->geometry().y()==gain->geometry().y()&&rows->geometry().right()<gain->geometry().left());
 assert(refresh->geometry().y()==apply->geometry().y()&&refresh->geometry().right()<apply->geometry().left());
 assert(apply->text()==QStringLiteral("Apply Settings"));
 assert(p.findChild<QSpinBox*>("nativeMinRows")->geometry().y()==p.findChild<QSpinBox*>("nativeMaxRows")->geometry().y());
 if(argc==2){p.resize(360,p.sizeHint().height());app.processEvents();assert(p.grab().save(QString::fromLocal8Bit(argv[1])));}
 for(const auto& style:QStyleFactory::keys()) {
  app.setStyle(style);
  for(bool chinese:{false,true})for(int points:{9,12,14}) {
   prism_viewer::common::setChineseUi(chinese);
   prism_viewer::ui::NativeCameraControlPanel narrow;
   auto font=narrow.font();font.setPointSize(points);narrow.setFont(font);
   narrow.setAvailability(true,true,false);narrow.setLive(s);
   narrow.resize(360,2800);narrow.show();app.processEvents();
   std::cout<<"layout style="<<style.toStdString()<<" zh="<<chinese<<" pt="<<points
            <<" minimum="<<narrow.minimumSizeHint().width()<<" actual="<<narrow.width()
            <<" font="<<narrow.font().family().toStdString()
            <<" narrow/wide glyph="<<narrow.fontMetrics().horizontalAdvance(QStringLiteral("i"))
            <<"/"<<narrow.fontMetrics().horizontalAdvance(QStringLiteral("W"))<<std::endl;
   if(narrow.minimumSizeHint().width()>360)for(auto* widget:narrow.findChildren<QWidget*>())
     std::cout<<"width diagnostic type="<<widget->metaObject()->className()
              <<" name="<<widget->objectName().toStdString()<<" minimum="<<widget->minimumSizeHint().width()
              <<" actual="<<widget->width()<<" fontpx="<<widget->fontMetrics().height()<<std::endl;
   assert(narrow.minimumSizeHint().width()<=360&&narrow.width()==360);
   for(auto* spin:narrow.findChildren<QAbstractSpinBox*>()) {
    const auto origin=spin->mapTo(&narrow,QPoint{});
    assert(spin->isVisible()&&origin.x()>=0&&origin.x()+spin->width()<=360);
   }
  }
 }
 prism_viewer::common::setChineseUi(false);
 std::cout<<"PASS native global AE/AGC/target/limits, per-camera manual rows/gain, epoch UI\n";
}
