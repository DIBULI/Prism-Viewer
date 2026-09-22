#include "ui/gnss_visualization.hpp"
#include "common/ui_text.hpp"
#include <QApplication>
#include <QTabWidget>
#include <QTableWidget>
#include <QLabel>
#include <QLineEdit>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
static std::string sentence(std::string body){unsigned crc=0;for(char c:body)crc^=uint8_t(c);char suffix[8];std::snprintf(suffix,sizeof(suffix),"*%02X",crc);return "$"+body+suffix;}
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
 b.records.clear();b.device_monotonic_ms=5001;w.apply(b);assert(table->rowCount()==0);
 w.unavailable("Disconnected");w.reset();app.processEvents();return 0;
}
