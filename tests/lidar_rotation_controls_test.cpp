#include "ui/lidar_rotation_controls.hpp"
#include "common/ui_text.hpp"
#include <QtCore/QProcess>
#include <QtCore/QSettings>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTimer>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QPushButton>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

using prism_viewer::LidarPointCloudWidget;
using prism_viewer::ui::LidarRotationControls;
void check(bool ok, const char* message) {
  if (!ok) {std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}
}
bool equal(LidarPointCloudWidget::DisplayRotation r, double x, double y, double z) {
  return std::abs(r.roll_degrees-x)<1e-8 && std::abs(r.pitch_degrees-y)<1e-8 &&
         std::abs(r.yaw_degrees-z)<1e-8;
}
int main(int argc,char** argv) {
  QApplication app(argc,argv);
  prism_viewer::common::setChineseUi(true);
  if(argc==3 && std::string(argv[1])=="--reload") {
    QSettings settings(QString::fromLocal8Bit(argv[2]),QSettings::IniFormat);
    LidarPointCloudWidget live,playback;QPushButton a,b;
    LidarRotationControls controls(settings,live,playback,a,b);
    check(equal(live.displayRotation(),30.25,-20.5,90.75) &&
          equal(playback.displayRotation(),30.25,-20.5,90.75),"persisted rotation after process restart");
    std::cout<<"PASS restarted settings\n";return 0;
  }
  QTemporaryDir temp;check(temp.isValid(),"temp");
  const QString file=temp.path()+"/settings.ini";
  QSettings settings(file,QSettings::IniFormat);
  LidarPointCloudWidget live,playback;QPushButton live_button,playback_button;
  LidarRotationControls controls(settings,live,playback,live_button,playback_button);
  check(equal(live.displayRotation(),0,0,0)&&equal(playback.displayRotation(),0,0,0),"default identity");
  prism::LidarPoint point{};point.x_mm=1000;
  live.appendPoints({point});playback.appendPoints({point});

  // Drive the same dialogs and entry points used in MainWindow.
  const auto edit = [&](QPushButton& entry, double r, double p, double y,
                        bool accept, bool reset) {
    QTimer::singleShot(0,[=]{
      auto* d=qobject_cast<QDialog*>(QApplication::activeModalWidget());
      check(d && d->objectName()=="lidarRotationDialog","rotation dialog opened");
      const char* names[]={"lidarRollDegrees","lidarPitchDegrees","lidarYawDegrees"};
      const double values[]={r,p,y};
      for(unsigned i=0;i<3;++i) {
        auto* s=d->findChild<QDoubleSpinBox*>(QLatin1String(names[i]));
        check(s && s->suffix()==QStringLiteral("°"),"angle input/degrees");
        s->setValue(values[i]);
      }
      auto* buttons=d->findChild<QDialogButtonBox*>();
      check(buttons,"dialog buttons");
      if(reset)buttons->button(QDialogButtonBox::Reset)->click();
      const QString shot=qEnvironmentVariable("PRISM_LIDAR_ROTATION_SCREENSHOT");
      if(!shot.isEmpty()&&accept&&!reset)check(d->grab().save(shot),"dialog screenshot");
      buttons->button(accept?QDialogButtonBox::Ok:QDialogButtonBox::Cancel)->click();
    });
    entry.click();
  };
  edit(live_button,30.25,-20.5,90.75,true,false);
  check(equal(live.displayRotation(),30.25,-20.5,90.75)&&
        equal(playback.displayRotation(),30.25,-20.5,90.75),"live edit updates both views");
  check(live_button.toolTip()==playback_button.toolTip()&&live_button.toolTip().contains("30.25"),
        "both entry points show current values");
  check(live.pointCount()==1&&playback.pointCount()==1,"existing points preserved");
  QProcess restart;
  restart.start(QCoreApplication::applicationFilePath(),{"--reload",file});
  check(restart.waitForFinished(20000),"settings restart timeout");
  std::cout<<restart.readAllStandardOutput().toStdString()<<restart.readAllStandardError().toStdString();
  check(restart.exitStatus()==QProcess::NormalExit&&restart.exitCode()==0,"restart failed");
  edit(playback_button,0,0,180,false,false);
  check(equal(live.displayRotation(),30.25,-20.5,90.75),"cancel does not apply");
  edit(playback_button,0,0,45,true,false);
  check(equal(live.displayRotation(),0,0,45)&&equal(playback.displayRotation(),0,0,45),
        "playback edit updates both views");
  live.resetView();playback.clearPoints();playback.appendPoints({point});
  check(equal(live.displayRotation(),0,0,45)&&equal(playback.displayRotation(),0,0,45),
        "view reset/dataset reopen do not reset stored transform");
  edit(playback_button,10,20,30,true,true);
  check(equal(live.displayRotation(),0,0,0)&&equal(playback.displayRotation(),0,0,0),"zero applied to both");
  QSettings persisted(file,QSettings::IniFormat);
  check(persisted.value(QLatin1String(LidarRotationControls::kSettingsKey)).toList()==QVariantList{0.,0.,0.},
        "zero saved");
  for(const auto& bad:QList<QVariant>{
      QVariantList{1.,2.},QVariantList{1.,QStringLiteral("bad"),3.},
      QVariantList{1.,std::numeric_limits<double>::quiet_NaN(),3.},
      QVariantList{1.,2.,999.}}) {
    settings.setValue(QLatin1String(LidarRotationControls::kSettingsKey),bad);
    LidarPointCloudWidget other_live,other_playback;QPushButton a,b;
    LidarRotationControls invalid(settings,other_live,other_playback,a,b);
    check(equal(other_live.displayRotation(),0,0,0)&&equal(other_playback.displayRotation(),0,0,0),
          "invalid preference safely defaults to identity");
  }
  std::cout<<"PASS shared live/replay entry points, cancel, zero, persistence/restart, malformed preferences\n";
}
