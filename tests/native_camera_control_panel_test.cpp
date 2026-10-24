#include "ui/native_camera_control_panel.hpp"
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpinBox>
#include <cassert>
#include <iostream>
int main(int argc,char** argv){
 QApplication app(argc,argv);prism_viewer::ui::NativeCameraControlPanel p;
 auto* apply=p.findChild<QPushButton*>("nativeApply");auto* refresh=p.findChild<QPushButton*>("nativeRefresh");
 auto* rows=p.findChild<QSpinBox*>("nativeRows2");auto* gain=p.findChild<QDoubleSpinBox*>("nativeGain2");
 auto* ae=p.findChild<QCheckBox*>("nativeAe2");auto* agc=p.findChild<QCheckBox*>("nativeAgc2");
 assert(apply&&refresh&&rows&&gain&&ae&&agc);assert(!apply->isEnabled());
 prism::CameraControlIdleStatus idle;idle.idle_epoch=7;idle.staged_revision=19;
 for(auto& c:idle.requested)c.manual_rows=1024;
 p.setAvailability(true,false,false);p.setIdle(idle);assert(apply->isEnabled()&&rows->value()==1024);
 unsigned calls=0;prism_viewer::NativeControlRequest last;
 p.on_request=[&](const auto& r){++calls;last=r;};
 rows->setValue(2048);gain->setValue(2.0625);ae->setChecked(true);agc->setChecked(true);assert(!rows->isEnabled()&&!gain->isEnabled());
 apply->click();assert(calls==1&&last.idle&&last.idle->staged_revision==19&&!last.live);
 assert(last.configuration[2].manual_rows==2048&&last.configuration[2].manual_gain_sixteenths==33&&last.configuration[2].automatic_exposure&&last.configuration[2].automatic_gain);
 ae->setChecked(false);agc->setChecked(false);gain->setValue(1.03);apply->click();assert(calls==1);
 gain->setValue(1);p.findChild<QSpinBox*>("nativeMinRows2")->setValue(3000);apply->click();assert(calls==1);
 p.setBusy(true);assert(!apply->isEnabled()&&!refresh->isEnabled());p.setBusy(false);
 p.setAvailability(true,true,false);assert(!apply->isEnabled()&&refresh->isEnabled());refresh->click();assert(calls==2&&!last.apply);
 prism::CameraControlStatus s;s.running=true;s.capture_epoch=77;s.config_revision=89;s.requested=idle.requested;
 s.observed[2].known=true;s.observed[2].fresh_at_snapshot=true;s.observed[2].rows=900;s.observed[2].gain_sixteenths=16;s.observed[2].source_frame_id=333;s.observed[2].config_revision=88;
 p.setLive(s);assert(rows->value()==1024&&apply->isEnabled());apply->click();assert(calls==3&&last.live&&last.live->capture_epoch==77&&last.live->config_revision==89&&!last.idle);
 p.setAvailability(true,false,false);assert(!apply->isEnabled());p.setIdle(idle);assert(apply->isEnabled());
 p.setAvailability(true,false,true);assert(!apply->isEnabled()&&!refresh->isEnabled());
 p.setAvailability(false,false,false);assert(!apply->isEnabled());
 p.setAvailability(true,false,false);p.setIdle(idle);p.resize(360,1800);p.show();app.processEvents();
 assert(p.minimumSizeHint().width()<=360);
 if(argc==2)assert(p.grab().save(QString::fromLocal8Bit(argv[1])));
 std::cout<<"PASS native per-camera rows/gain/AE/AGC/limits/epoch UI\n";
}
