#include "ui/device_logs_dialog.hpp"
#include "common/ui_text.hpp"
#include <QDialog>
#include <QCloseEvent>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <atomic>
#include <memory>
namespace prism_viewer::ui {
using common::uiText;
namespace {
class Dialog final:public QDialog {
  QLabel* status_;QProgressBar* progress_;QPushButton* close_;
  QThread* worker_=nullptr;std::atomic<bool> cancel_{false};
  QString destination_;CollectDeviceLogs collect_;
  struct Result {QString error;bool cancelled=false,partial=false;};
  void start() {
    auto result=std::make_shared<Result>();
    worker_=QThread::create([this,result] {
      try {
        auto archive=collect_([this](uint32_t completed,uint32_t total){
          QMetaObject::invokeMethod(this,[this,completed,total]{
            progress_->setRange(0,int(total));progress_->setValue(int(completed));
          },Qt::QueuedConnection);
        },[this]{return cancel_.load();});
        if(cancel_)throw prism::DeviceLogCancelled();
        if(archive.zip.empty())throw std::runtime_error("Empty device log archive");
        // Qt handles Unicode paths; failed/cancelled exports never replace the
        // destination with a half-written ZIP. No device-side files are deleted.
        QSaveFile file(destination_);file.setDirectWriteFallback(false);
        if(!file.open(QIODevice::WriteOnly)||
            file.write(reinterpret_cast<const char*>(archive.zip.data()),qint64(archive.zip.size()))!=qint64(archive.zip.size()))
          throw std::runtime_error(file.errorString().toStdString());
        if(cancel_){file.cancelWriting();throw prism::DeviceLogCancelled();}
        if(!file.commit())throw std::runtime_error(file.errorString().toStdString());
        result->partial=archive.partial;
      }catch(const prism::DeviceLogCancelled&){result->cancelled=true;}
      catch(const std::exception& e){result->error=QString::fromUtf8(e.what());}
    });
    connect(worker_,&QThread::finished,this,[this,result]{
      auto* finished=worker_;worker_=nullptr;finished->deleteLater();progress_->setRange(0,4);
      close_->setText(uiText("Close","关闭"));close_->setEnabled(true);
      if(result->cancelled)status_->setText(uiText("Cancelled. No archive was saved.","已取消，未保存压缩包。"));
      else if(!result->error.isEmpty())status_->setText(uiText("Device log export failed: ","设备日志导出失败：")+result->error);
      else {
        progress_->setValue(4);
        status_->setText((result->partial?
          uiText("Saved available device logs. Some sections are unavailable or limited; see manifest.json.\n", "已保存可用设备日志。部分日志缺失或达到截取上限，详情见 manifest.json。\n"):
          uiText("Device logs saved.\n","设备日志已保存。\n"))+destination_);
      }
    });worker_->start();
  }
 public:
  Dialog(QWidget* parent,QString destination,CollectDeviceLogs collect):QDialog(parent),
      destination_(std::move(destination)),collect_(std::move(collect)) {
    setObjectName("deviceLogsDialog");setWindowTitle(uiText("Export device logs","导出设备日志"));resize(620,240);
    auto* layout=new QVBoxLayout(this);
    auto* note=new QLabel(uiText("Agent 1.4.0 or later with log export support. Downloads recent service and kernel logs from the current and previous boot over this connection, then compresses them on this computer.",
      "需要支持日志导出的 Agent 1.4.0 或更高版本。通过当前连接下载本次和上次启动的近期服务、内核日志，在电脑端压缩保存。"),this);
    note->setWordWrap(true);layout->addWidget(note);
    status_=new QLabel(uiText("Reading device logs…","正在读取设备日志……"),this);status_->setObjectName("deviceLogsStatus");
    status_->setWordWrap(true);status_->setTextFormat(Qt::PlainText);status_->setTextInteractionFlags(Qt::TextSelectableByMouse);layout->addWidget(status_);
    progress_=new QProgressBar(this);progress_->setRange(0,4);progress_->setValue(0);layout->addWidget(progress_);
    close_=new QPushButton(uiText("Cancel","取消"),this);close_->setObjectName("deviceLogsClose");layout->addWidget(close_);
    connect(close_,&QPushButton::clicked,this,[this]{reject();});QTimer::singleShot(0,this,[this]{start();});
  }
  ~Dialog()override{if(worker_){cancel_=true;worker_->disconnect(this);worker_->wait();delete worker_;}}
  void reject()override{if(worker_){cancel_=true;close_->setEnabled(false);status_->setText(uiText("Cancelling…","正在取消……"));}else QDialog::reject();}
  void closeEvent(QCloseEvent* event)override{if(worker_){reject();event->ignore();}else QDialog::closeEvent(event);}
};
}
void showDeviceLogsDialog(QWidget* parent,const QString& destination,CollectDeviceLogs collect){Dialog dialog(parent,destination,std::move(collect));dialog.exec();}
}
