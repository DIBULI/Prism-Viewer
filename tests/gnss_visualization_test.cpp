#include "ui/gnss_visualization.hpp"
#include "ui/trajectory_view.hpp"
#include "common/ui_text.hpp"
#include <QApplication>
#include <QTabWidget>
#include <QTableWidget>
#include <QLabel>
#include <QLineEdit>
#include <QThread>
#include <QPushButton>
#include <QCheckBox>
#include <QWheelEvent>
#include <QMouseEvent>
#include <limits>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
static std::string sentence(std::string body){unsigned crc=0;for(char c:body)crc^=uint8_t(c);char suffix[8];std::snprintf(suffix,sizeof(suffix),"*%02X",crc);return "$"+body+suffix;}
static std::string rtkSentence(const std::string& north="0.012",const std::string& east="0.034",const std::string& up="0.056",
                              const std::string& solution="NARROW_INT",const std::string& status="SOL_COMPUTED",
                              const std::string& lat="31",const std::string& lon="121",const std::string& epoch="388818100") {
 const auto body="ADRNAVA,COM1,GPS,FINE,2437,"+epoch+",0,0,18,0;"+status+","+solution+
     ","+lat+","+lon+",20,10,WGS84,"+north+","+east+","+up+",0,0,0,20,18,0,0,0,0,0,0,0,0,0,0,0,0,0,0";
 uint32_t crc=0;for(unsigned char c:body){crc^=c;for(int i=0;i<8;++i)crc=(crc>>1)^((crc&1)?0xedb88320u:0);}
 char suffix[10];std::snprintf(suffix,sizeof(suffix),"*%08X",crc);return "#"+body+suffix;
}
static void zoomGeometry(){
 using prism_viewer::ui::TrajectoryView;
 TrajectoryView view;const QRectF area(36,40,900,400),small(-.002,-.003,.004,.006);
 view.fit(small,area);assert(view.pixels_per_meter>10000); // No former 1 m floor.
 for(const auto scale:{10.,100.,1000.,10000.,100000.}){
   view.pixels_per_meter=scale;const auto m=view.rulerMeters();
   assert(m>0&&m*scale<=100.000001&&m*scale>=39.999);
   assert(std::abs(QLineF(view.screen({0,0},area),view.screen({m,0},area)).length()-m*scale)<1e-8);
 }
 assert(TrajectoryView::distance(2)=="2 m");assert(TrajectoryView::distance(.02)=="2 cm");
 assert(TrajectoryView::distance(.002)=="2 mm");assert(TrajectoryView::distance(.001)=="1 mm");
 const QPointF cursor(250,130);const auto at=view.world(cursor,area);
 view.zoom(.25,cursor,area);assert(QLineF(at,view.world(cursor,area)).length()<1e-10);
 assert(!view.automatic);const double locked=view.pixels_per_meter;const auto center=view.center;
 view.fit(QRectF(-500,-500,1000,1000),area);assert(view.pixels_per_meter==locked&&view.center==center);
 view.pan({80,-40});assert(QLineF(view.screen(at,area),cursor+QPointF(80,-40)).length()<1e-8);
 view.zoom(1e100,cursor,area);assert(view.pixels_per_meter==TrajectoryView::max_scale);
 assert(view.rulerMeters()==.001);
 view.zoom(1e-100,cursor,area);assert(view.pixels_per_meter==TrajectoryView::min_scale);
 view.zoom(std::numeric_limits<double>::quiet_NaN(),cursor,area);assert(std::isfinite(view.pixels_per_meter));
 view.reset();view.fit(small,area);assert(view.automatic&&view.pixels_per_meter>10000);
}
static void zoomWidgets(QApplication& app,const QString& output){
 prism_viewer::common::setChineseUi(true);
 prism_viewer::ui::GnssVisualization w;w.resize(1100,760);w.show();app.processEvents();
 auto* tabs=w.findChild<QTabWidget*>();auto* top=w.findChild<QCheckBox*>("trajectoryTopView");assert(tabs&&top);
 top->setChecked(true);tabs->setCurrentIndex(2);app.processEvents();
 auto* rtk=w.findChild<QWidget*>("rtkTrajectory");auto* gnss=w.findChild<QWidget*>("gnssTrajectory");assert(rtk&&gnss);
 auto* in=w.findChild<QPushButton*>("rtkZoomIn");auto* out=w.findChild<QPushButton*>("rtkZoomOut");
 auto* fit=w.findChild<QPushButton*>("rtkFitTrajectory");assert(in&&out&&fit);
 prism::GnssObservations b;b.session=42;b.device_monotonic_ms=1000;
 // Synthetic mm-scale ENU variation, not hardware measurements or solver output.
 auto append=[&](int i,double offset){
   const auto lat=QString::number(31+offset+std::sin(i*.31)*2e-8,'f',10).toStdString();
   const auto lon=QString::number(121+std::cos(i*.21)*3e-8,'f',10).toStdString();
   b.records={{++b.cursor,1000,rtkSentence("0.012","0.034","0.056","NARROW_INT","SOL_COMPUTED",lat,lon,std::to_string(388818100+i))}};
   w.apply(b);app.processEvents();
 };
 for(int i=0;i<100;++i)append(i,0);
 assert(rtk->accessibleDescription().contains("mm"));assert(rtk->accessibleDescription().contains("自动适应"));
 for(int i=0;i<10;++i)in->click();app.processEvents();
 assert(rtk->accessibleDescription().contains("1 mm"));assert(rtk->accessibleDescription().contains("手动缩放"));
 if(!output.isEmpty())assert(w.grab().save(output+"-rtk-mm.png"));
 const auto manual=rtk->accessibleDescription();append(100,.001);
 assert(rtk->accessibleDescription()==manual); // Incoming outlier cannot zoom away.
 // UI wheel and trackpad both affect the actual plot, not only geometry helpers.
 out->click();app.processEvents();
 const QPointF anchor(rtk->rect().center());
 QWheelEvent wheel(anchor,rtk->mapToGlobal(anchor.toPoint()),{},QPoint(0,-1200),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
 QApplication::sendEvent(rtk,&wheel);app.processEvents();assert(rtk->accessibleDescription()!=manual);
 const auto wheeled=rtk->accessibleDescription();
 assert(wheeled.contains("cm"));
 if(!output.isEmpty())assert(w.grab().save(output+"-rtk-cm.png"));
 QWheelEvent pad(anchor,rtk->mapToGlobal(anchor.toPoint()),QPoint(0,-160),{},Qt::NoButton,Qt::NoModifier,Qt::ScrollUpdate,false);
 QApplication::sendEvent(rtk,&pad);app.processEvents();assert(rtk->accessibleDescription()!=wheeled);
 fit->click();app.processEvents();assert(rtk->accessibleDescription().contains("自动适应"));
 assert(rtk->accessibleDescription().contains(" m"));
 if(!output.isEmpty())assert(w.grab().save(output+"-rtk-meters.png"));
 top->setChecked(false);app.processEvents();assert(rtk->accessibleDescription().contains("投影比例尺"));
 if(!output.isEmpty())assert(w.grab().save(output+"-rtk-3d.png"));
 in->click();app.processEvents();assert(rtk->accessibleDescription().contains("手动缩放"));
 tabs->setCurrentIndex(1);app.processEvents();assert(gnss->accessibleDescription().contains("自动适应"));
 w.findChild<QPushButton*>("gnssZoomIn")->click();app.processEvents();assert(gnss->accessibleDescription().contains("手动缩放"));
 tabs->setCurrentIndex(2);app.processEvents();assert(rtk->accessibleDescription().contains("手动缩放"));
 QMouseEvent dbl(QEvent::MouseButtonDblClick,anchor,rtk->mapToGlobal(anchor.toPoint()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
 QApplication::sendEvent(rtk,&dbl);app.processEvents();assert(rtk->accessibleDescription().contains("自动适应"));
 in->click();app.processEvents();b.session++;b.records.clear();w.apply(b);app.processEvents();
 assert(rtk->accessibleDescription().contains("自动适应"));
 in->click();app.processEvents();w.findChild<QPushButton*>("clearTrajectories")->click();app.processEvents();
 assert(rtk->accessibleDescription().contains("自动适应"));
 // Narrow layout and English rendering; no data access or connection.
 w.resize(600,650);app.processEvents();
 if(!output.isEmpty())assert(w.grab().save(output+"-rtk-narrow-zh.png"));
 w.hide();prism_viewer::common::setChineseUi(false);
 prism_viewer::ui::GnssVisualization english;english.resize(600,650);english.show();
 english.findChild<QTabWidget*>()->setCurrentIndex(2);app.processEvents();
 assert(english.findChild<QPushButton*>("rtkFitTrajectory")->text()=="Fit trajectory");
 if(!output.isEmpty())assert(english.grab().save(output+"-rtk-narrow-en.png"));
 std::puts("Trajectory zoom: meter/cm/mm ruler, cursor anchor, bounded extremes, live lock, independent pages, wheel/trackpad and fit/reset passed");
}
int main(int argc,char** argv){
 QApplication app(argc,argv);prism_viewer::common::setChineseUi(true);
 zoomGeometry();
 prism_viewer::ui::GnssVisualization w;w.resize(1280,720);w.show();
 prism::GnssObservations b;b.session=1;b.cursor=3;b.device_monotonic_ms=1000;
 b.records={{1,1000,sentence("GNGGA,120000.1,3100.0000,N,12100.0000,E,1,12,0.8,20,M,10,M,,")},
 {2,1000,sentence("GPGSV,1,1,03,01,45,000,42,02,30,120,38,03,65,250,40,1")},
 {3,1000,sentence("GBGSV,1,1,03,19,35,050,43,20,50,190,41,21,25,310,39,1")}};
 w.apply(b);app.processEvents();auto* table=w.findChild<QTableWidget*>();assert(table&&table->rowCount()==6);
 table->selectRow(1);app.processEvents();
 if(argc>1)assert(w.grab().save(QString::fromUtf8(argv[1])+"-sky.png"));
 auto* tabs=w.findChild<QTabWidget*>();assert(tabs&&tabs->count()==3);tabs->setCurrentIndex(1);app.processEvents();
 if(argc>1)assert(w.grab().save(QString::fromUtf8(argv[1])+"-track.png"));
 auto* filter=w.findChild<QLineEdit*>();filter->setText("BeiDou");assert(table->rowCount()==3);
 auto* rtk=w.findChild<QLabel*>("rtkReceiverPosition");auto* gnss=w.findChild<QLabel*>("gnssPosition");assert(rtk&&gnss);
 auto applyRtk=[&](const std::string& s){b.records={{++b.cursor,b.device_monotonic_ms,s}};w.apply(b);app.processEvents();};
 auto noOld=[&]{const auto text=rtk->text();assert(!text.contains("0.012")&&!text.contains("0.034")&&!text.contains("0.056"));
   assert(text.contains("N —")&&text.contains("E —")&&text.contains("U —"));};
 applyRtk(rtkSentence());tabs->setCurrentIndex(2);app.processEvents();
 assert(rtk->text().contains("北向 N 0.012 · 东向 E 0.034 · 高程 U 0.056"));
 assert(rtk->text().contains("1σ，m")&&rtk->text().contains("不代表实际误差保证"));
 assert(!gnss->text().contains("1σ"));
 if(argc>1)assert(w.grab().save(QString::fromUtf8(argv[1])+"-rtk-precision-zh.png"));
 prism_viewer::common::setChineseUi(false);applyRtk(rtkSentence("0.012","0.034","0.056","NARROW_FLOAT"));
 assert(rtk->text().contains("North N 0.012 · East E 0.034 · Up U 0.056"));
 assert(rtk->text().contains("1σ, m")&&rtk->text().contains("not guaranteed actual errors"));
 assert(rtk->text().contains("NARROW_FLOAT"));
 if(argc>1)assert(w.grab().save(QString::fromUtf8(argv[1])+"-rtk-precision-en.png"));
 applyRtk(rtkSentence("","0","0.056"));assert(rtk->text().contains("North N — · East E 0.000 · Up U 0.056"));
 for(const auto& s:{"","-1","nan","inf"}){applyRtk(rtkSentence(s,s,s));noOld();}
 applyRtk(rtkSentence("0.012","0.034","0.056","NARROW_INT","INSUFFICIENT_OBS"));noOld();
 applyRtk(rtkSentence());w.unavailable("Disconnected");noOld();
 applyRtk(rtkSentence());assert(rtk->text().contains("North N 0.012"));
 // Expiry advances with host elapsed time, even if no new batch arrives.
 QThread::msleep(2050);app.processEvents();noOld();
 applyRtk(rtkSentence());b.gap=true;b.records.clear();w.apply(b);noOld();b.gap=false;
 applyRtk(rtkSentence());b.session=2;b.records.clear();w.apply(b);noOld();
 b.records.clear();b.device_monotonic_ms=5001;w.apply(b);assert(table->rowCount()==0);
 w.unavailable("Disconnected");w.reset();noOld();app.processEvents();
 std::puts("RTK precision display: zh/en, FIX/FLOAT, axis order, missing/zero, invalid, expiry, gap, disconnect and reset passed");
 w.hide();zoomWidgets(app,argc>1?QString::fromUtf8(argv[1]):QString());return 0;
}
