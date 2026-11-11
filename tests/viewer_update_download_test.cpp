#include "ui/viewer_update_dialog.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QPushButton>
#include <QTemporaryDir>
#include <iostream>
#include <cstring>
#include <stdexcept>
namespace prism_viewer::ui {
struct UpdateDialogHarness {
  static void check(ViewerUpdateDialog& d){d.check();}
  static bool ready(ViewerUpdateDialog& d){return d.download_->isEnabled();}
  static bool active(ViewerUpdateDialog& d){return !d.reply_.isNull();}
  static void save(ViewerUpdateDialog& d,const QString& p){d.downloadTo(p);}
  static void download(ViewerUpdateDialog& d){d.download();}
  static QString path(ViewerUpdateDialog& d){return d.path_;}
  static void cancel(ViewerUpdateDialog& d){d.stop();}
  static void timeout(ViewerUpdateDialog& d){QMetaObject::invokeMethod(&d.timeout_,"timeout",Qt::DirectConnection);}
};
}
using prism_viewer::ui::ViewerUpdateDialog;
using H=prism_viewer::ui::UpdateDialogHarness;
void check(bool v){if(!v)throw std::runtime_error("download assertion failed");}
class Reply:public QNetworkReply {
  QByteArray bytes_;qint64 pos_=0;
 public:
  Reply(const QNetworkRequest& request,QByteArray bytes,bool hold,bool tls,bool redirect,QObject* parent):QNetworkReply(parent),bytes_(std::move(bytes)){
    setRequest(request);setUrl(request.url());setAttribute(QNetworkRequest::HttpStatusCodeAttribute,200);open(QIODevice::ReadOnly);
    QTimer::singleShot(0,this,[this,hold,tls,redirect]{if(redirect){emit redirected(QUrl("https://github.com/DIBULI/Prism-Viewer/releases/download/v1.3.0/package.zip"));return;}if(tls)setError(SslHandshakeFailedError,"TLS rejected");else emit readyRead();if(!hold){setFinished(true);emit finished();}});
  }
  void abort()override{setError(OperationCanceledError,"cancelled");setFinished(true);emit finished();}
  qint64 bytesAvailable()const override{return bytes_.size()-pos_+QIODevice::bytesAvailable();}
  qint64 readData(char* data,qint64 max)override{const auto n=std::min(max,qint64(bytes_.size()-pos_));if(!n)return -1;std::memcpy(data,bytes_.constData()+pos_,size_t(n));pos_+=n;return n;}
};
class Network:public QNetworkAccessManager {
 public:
  QByteArray package="verified package contents";
  QString fault;
 protected:
  QNetworkReply* createRequest(Operation,const QNetworkRequest& request,QIODevice*)override{
    const auto url=request.url();QByteArray bytes;bool hold=false;
    check(url.host()=="dibuli.com"&&prism_viewer::update::trustedUrl(url));
    if(url==prism_viewer::update::manifestUrl()){
      check(request.attribute(QNetworkRequest::CacheLoadControlAttribute)==QNetworkRequest::AlwaysNetwork);
      const auto target=prism_viewer::update::platform();
      QJsonObject asset{{"name","Prism-Viewer-1.3.0-"+target+(target.startsWith("linux-")?".tar.gz":".zip")},{"size",package.size()},
          {"sha256",QString::fromLatin1(QCryptographicHash::hash(package,QCryptographicHash::Sha256).toHex())}};
      bytes=QJsonDocument(QJsonObject{{"schema",1},{"version","1.3.0"},{"revision",QString(40,'b')},{"build_epoch",500},{"platforms",QJsonObject{{target,asset}}}}).toJson();
    }else{bytes=fault=="crc"?QByteArray(package.size(),'x'):package;if(fault=="short")bytes.chop(1);if(fault=="long")bytes+='x';hold=fault=="cancel"||fault=="timeout";}
    return new Reply(request,bytes,hold,fault=="tls",fault=="redirect",this);
  }
};
template<class F>void until(F f){QElapsedTimer t;t.start();while(!f()){check(t.elapsed()<2000);QCoreApplication::processEvents();}}
int main(int argc,char** argv){QApplication app(argc,argv);try{
  QString temporaryPackage;
  {
    Network network;ViewerUpdateDialog dialog(nullptr,[]{return false;},&network);
    H::check(dialog);until([&]{return !H::active(dialog);});check(H::ready(dialog));
    H::download(dialog);until([&]{return !H::active(dialog);});temporaryPackage=H::path(dialog);
    check(QFileInfo(temporaryPackage).absolutePath().startsWith(QDir::tempPath()+"/prism-viewer-download-"));
    QFile f(temporaryPackage);check(f.open(QIODevice::ReadOnly));check(f.readAll()==network.package);
  }
  check(!QFile::exists(temporaryPackage));
  std::cout<<"PASS automatic system-temp download and cleanup\n";
  for(const auto& fault:QStringList{"","crc","short","long","cancel","timeout","tls","redirect"}){
    QTemporaryDir temp;check(temp.isValid());const auto path=temp.path()+"/update.zip";
    {QFile old(path);check(old.open(QIODevice::WriteOnly));old.write("old file");}
    Network network;ViewerUpdateDialog dialog(nullptr,[]{return false;},&network);
    H::check(dialog);until([&]{return !H::active(dialog);});check(H::ready(dialog));
    network.fault=fault;H::save(dialog,path);
    if(fault=="cancel")H::cancel(dialog);
    if(fault=="timeout")H::timeout(dialog);
    until([&]{return !H::active(dialog);});
    QFile actual(path);check(actual.open(QIODevice::ReadOnly));check(actual.readAll()==(fault.isEmpty()?network.package:QByteArray("old file")));
    std::cout<<"PASS "<<(fault.isEmpty()?"verified":fault.toStdString())<<'\n';
  }
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
