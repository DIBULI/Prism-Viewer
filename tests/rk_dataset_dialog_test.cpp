#include "ui/rk_dataset_dialog.hpp"
#include "ui/app_theme.hpp"
#include "common/ui_text.hpp"
#include <QtCore/QSettings>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTimer>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QTableWidget>
#include <iostream>
int main(int argc,char** argv) {
  QApplication app(argc,argv);prism_viewer::ui::applyLightApplicationTheme(app);
  QTemporaryDir settings;QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
  prism_viewer::dataset::RkDatasetAccess access;
  access.list=[] {std::vector<prism::RecordedDataset> rows;
    for(int i=0;i<4;++i)rows.push_back({"capture-test-"+std::to_string(i),123,i!=2,i!=3});return rows;};
  for(bool chinese:{false,true}) {
    prism_viewer::common::setChineseUi(chinese);bool passed=false;
    QTimer timeout,poll;timeout.setSingleShot(true);
    QObject::connect(&timeout,&QTimer::timeout,&app,[]{std::exit(2);});
    QObject::connect(&poll,&QTimer::timeout,&app,[&]{
      auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)return;
      auto* table=dialog->findChild<QTableWidget*>("rkDatasetTable");
      auto* download=dialog->findChild<QPushButton*>("rkDatasetDownload");
      auto* formats=dialog->findChild<QComboBox*>("rkDatasetFormat");
      if(!table||table->rowCount()!=4||!download->isEnabled())return;
      passed=formats&&formats->count()==3&&table->currentRow()==0&&table->columnCount()==4&&
        !dialog->findChild<QLineEdit*>("rkDatasetAddress");
      const auto prefix=qEnvironmentVariable("PRISM_RK_DIALOG_SCREENSHOT");
      if(!prefix.isEmpty())passed=dialog->grab().save(prefix+(chinese?"-zh.png":"-en.png"))&&passed;
      dialog->reject();
    });
    QTimer::singleShot(0,&app,[]{auto* d=QApplication::activeModalWidget();if(d)d->findChild<QPushButton*>("rkDatasetRefresh")->click();});
    timeout.start(10000);poll.start(50);
    prism_viewer::ui::showRkDatasetDialog(nullptr,access,[](const QString&){});
    if(!passed)return 3;
  }
  std::cout<<"PASS USB dataset dialog: bilingual, asynchronous list, no IP/HTTP fields, Viewer-only bag selection\n";return 0;
}
