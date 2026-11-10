#include "ui/viewer_update_dialog.hpp"
#include "common/ui_text.hpp"
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QLabel>
#include <QNetworkReply>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <functional>
#include <stdexcept>

namespace prism_viewer::ui {
using common::uiText;
ViewerUpdateDialog::ViewerUpdateDialog(QWidget* parent,std::function<bool()> can_restart,QNetworkAccessManager* network):QDialog(parent),can_restart_(std::move(can_restart)),network_(network?network:new QNetworkAccessManager(this)){
  setWindowTitle(uiText("Viewer updates","Viewer 在线更新"));resize(560,380);
  auto* layout=new QVBoxLayout(this);
  layout->addWidget(new QLabel(uiText("Installed: %1 (%2)","当前版本：%1（%2）")
      .arg(QStringLiteral(PRISM_VIEWER_VERSION),QStringLiteral(PRISM_VIEWER_REVISION).left(8)),this));
  status_=new QLabel(uiText("Checks and downloads updates from dibuli.com.",
      "通过官网 dibuli.com 检查和下载更新。"),this);status_->setWordWrap(true);layout->addWidget(status_);
  notes_=new QPlainTextEdit(this);notes_->setReadOnly(true);layout->addWidget(notes_);
  progress_=new QProgressBar(this);progress_->hide();layout->addWidget(progress_);
  auto* buttons=new QHBoxLayout;
  check_=new QPushButton(uiText("Check for updates","检查更新"),this);
  download_=new QPushButton(uiText("Download and update","下载并更新"),this);download_->setEnabled(false);
  open_=new QPushButton(uiText("Install and restart","安装并重启"),this);open_->setEnabled(false);
  cancel_=new QPushButton(uiText("Cancel download","取消下载"),this);cancel_->setEnabled(false);
  for(auto* b:{check_,download_,open_,cancel_})buttons->addWidget(b);layout->addLayout(buttons);
  connect(check_,&QPushButton::clicked,this,[this]{check();});
  connect(download_,&QPushButton::clicked,this,[this]{download();});
  connect(open_,&QPushButton::clicked,this,[this]{install();});
  connect(cancel_,&QPushButton::clicked,this,[this]{stop();status_->setText(uiText("Cancelled; installed Viewer unchanged.","已取消，现有 Viewer 未改变。"));});
  timeout_.setSingleShot(true);connect(&timeout_,&QTimer::timeout,this,[this]{fail(uiText("Update request timed out. Try again later.","更新请求超时，请稍后再试。"));});
  connect(this,&QDialog::finished,this,[this]{stop();});
}
ViewerUpdateDialog::~ViewerUpdateDialog(){stop();}
void ViewerUpdateDialog::stop(){
  timeout_.stop();if(reply_){auto* r=reply_.data();reply_=nullptr;r->disconnect(this);r->abort();r->deleteLater();}
  if(file_.isOpen()){file_.cancelWriting();(void)file_.commit();}
  check_->setEnabled(true);cancel_->setEnabled(false);progress_->hide();
}
void ViewerUpdateDialog::fail(const QString& message){stop();download_->setEnabled(false);status_->setText(message);}
void ViewerUpdateDialog::check(){
  stop();download_->setEnabled(false);open_->setEnabled(false);notes_->clear();
  status_->setText(uiText("Checking official release…","正在检查官方发布…"));
  request(update::manifestUrl(),Phase::Manifest);
}
void ViewerUpdateDialog::download(){
  if(reply_||release_.url.isEmpty())return;
  const auto path=QFileDialog::getSaveFileName(this,uiText("Save update package","保存更新包"),
      QDir(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)).filePath(release_.name));
  if(path.isEmpty())return;
  downloadTo(path);
}
void ViewerUpdateDialog::downloadTo(const QString& path){
  path_=path;file_.setFileName(path);file_.setDirectWriteFallback(false);
  if(!file_.open(QIODevice::WriteOnly)){fail(file_.errorString());return;}
  downloaded_=0;hash_.reset();download_->setEnabled(false);open_->setEnabled(false);
  progress_->setRange(0,100);progress_->setValue(0);progress_->show();
  status_->setText(uiText("Downloading and verifying update…","正在下载并校验更新包…"));request(release_.url,Phase::Package);
}
void ViewerUpdateDialog::install(){
  if(!can_restart_()){
    status_->setText(uiText("Update downloaded. Stop capture/recording and close the device connection, then install.",
      "更新已下载。请先停止采集/录制并关闭设备连接，再点击安装并重启。"));return;
  }
  if(installer_)delete installer_.data();
  check_->setEnabled(false);download_->setEnabled(false);open_->setEnabled(false);cancel_->setEnabled(false);
  status_->setText(uiText("Preparing update; Viewer will exit and restart automatically.","正在准备更新，完成后 Viewer 将退出并自动重启。"));
  setWindowModality(Qt::ApplicationModal);
  installer_=new update::Installer(this,can_restart_,[this](const QString& message){
    status_->setText(message);check_->setEnabled(true);open_->setEnabled(true);setWindowModality(Qt::NonModal);
  });
  installer_->start(path_,release_);
}
void ViewerUpdateDialog::request(const QUrl& url,Phase phase){
  if(!update::trustedUrl(url)){fail(uiText("Unexpected update server.","更新服务器地址异常。"));return;}
  metadata_.clear();check_->setEnabled(false);cancel_->setEnabled(true);
  QNetworkRequest request(url);request.setRawHeader("User-Agent","Prism-Viewer/" PRISM_VIEWER_VERSION);
  request.setRawHeader("Accept",phase==Phase::Manifest?"application/json":"application/octet-stream");
  request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,QNetworkRequest::AlwaysNetwork);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::UserVerifiedRedirectPolicy);
  auto* r=network_->get(request);reply_=r;r->setReadBufferSize(256*1024);timeout_.start(30000);
  connect(r,&QNetworkReply::redirected,this,[this,r](const QUrl& next){
    if(update::trustedUrl(next))r->redirectAllowed();else fail(uiText("Update redirect rejected.","更新下载跳转地址异常。"));
  });
  const auto read=[this,r,phase]{
    if(reply_!=r)return;
    const auto chunk=r->readAll();timeout_.start(30000);
    // Redirect bodies are not package bytes.
    const int status=r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if(status!=200)return;
    if(phase==Phase::Package){
      if(chunk.size()>release_.size-downloaded_||file_.write(chunk)!=chunk.size()){
        fail(uiText("Invalid download size or storage write failure.","下载长度异常或本地写入失败。"));return;}
      hash_.addData(chunk);downloaded_+=chunk.size();progress_->setValue(int(downloaded_*100/release_.size));
    }else{
      if(metadata_.size()+chunk.size()>1024*1024){fail(uiText("Update metadata too large.","更新信息超出大小限制。"));return;}
      metadata_+=chunk;
    }
  };
  connect(r,&QIODevice::readyRead,this,read);
  connect(r,&QNetworkReply::finished,this,[this,r,phase,read]{
    read();if(reply_!=r)return;timeout_.stop();reply_=nullptr;r->deleteLater();
    check_->setEnabled(true);cancel_->setEnabled(false);
    if(r->error()!=QNetworkReply::NoError||r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()!=200){
      fail(uiText("Update request failed: %1","更新请求失败：%1").arg(r->errorString()));return;}
    try{
      if(phase==Phase::Manifest){
        release_=update::parseManifest(metadata_,update::platform());notes_->setPlainText(release_.notes);
        const bool available=update::newer(release_,QStringLiteral(PRISM_VIEWER_VERSION),PRISM_VIEWER_BUILD_EPOCH,QStringLiteral(PRISM_VIEWER_REVISION));
        download_->setEnabled(available);status_->setText(available?
          uiText("Update available: %1 (%2), %3 MiB","可更新：%1（%2），%3 MiB").arg(release_.version,release_.revision.left(8)).arg(release_.size/1048576.):
          uiText("No newer release available.","暂无更新的发布版本。"));return;
      }
      if(downloaded_!=release_.size||hash_.result()!=release_.sha256)throw std::runtime_error("Update checksum/size mismatch; package discarded");
      if(!file_.commit())throw std::runtime_error(file_.errorString().toStdString());
      progress_->setValue(100);open_->setEnabled(true);
      status_->setText(uiText("Verified: %1","更新包已校验：%1").arg(path_));install();
    }catch(const std::exception& e){fail(QString::fromUtf8(e.what()));}
  });
}
}
