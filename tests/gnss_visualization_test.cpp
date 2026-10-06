#include "ui/gnss_visualization.hpp"
#include "common/ui_text.hpp"
#include <QApplication>
#include <QTabWidget>
#include <QTableWidget>
#include <QLabel>
#include <QLineEdit>
#include <QThread>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
static std::string sentence(std::string body){unsigned crc=0;for(char c:body)crc^=uint8_t(c);char suffix[8];std::snprintf(suffix,sizeof(suffix),"*%02X",crc);return "$"+body+suffix;}
static std::string rtkSentence(const std::string& north="0.012",const std::string& east="0.034",const std::string& up="0.056",
                              const std::string& solution="NARROW_INT",const std::string& status="SOL_COMPUTED") {
 const auto body="ADRNAVA,COM1,GPS,FINE,2437,388818100,0,0,18,0;"+status+","+solution+
     ",31,121,20,10,WGS84,"+north+","+east+","+up+",0,0,0,20,18,0,0,0,0,0,0,0,0,0,0,0,0,0,0";
 uint32_t crc=0;for(unsigned char c:body){crc^=c;for(int i=0;i<8;++i)crc=(crc>>1)^((crc&1)?0xedb88320u:0);}
 char suffix[10];std::snprintf(suffix,sizeof(suffix),"*%08X",crc);return "#"+body+suffix;
}
int main(int argc,char** argv){
 QApplication app(argc,argv);prism_viewer::common::setChineseUi(true);
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
 std::puts("RTK precision display: zh/en, FIX/FLOAT, axis order, missing/zero, invalid, expiry, gap, disconnect and reset passed");return 0;
}
