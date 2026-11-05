#include "update/install.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>
#include <QTemporaryDir>
#include <stdexcept>
#include <memory>

namespace prism_viewer::update {
namespace {
void need(bool v,const char* text){if(!v)throw std::runtime_error(text);}
QString script(const QString& work,const QString& name){
  QFile source(":/update/"+name);need(source.open(QIODevice::ReadOnly),"Missing update helper");
  const auto bytes=source.readAll();const auto path=work+"/"+name;QFile out(path);
  need(out.open(QIODevice::WriteOnly|QIODevice::NewOnly)&&out.write(bytes)==bytes.size(),"Cannot prepare update helper");
  out.close();return path;
}
#if defined(Q_OS_WIN)
QString powershell(){return qEnvironmentVariable("SystemRoot")+"/System32/WindowsPowerShell/v1.0/powershell.exe";}
#endif
}
Installer::Installer(QObject* owner,std::function<bool()> idle,std::function<void(QString)> result):
    QObject(owner),idle_(std::move(idle)),result_(std::move(result)){
  timeout_.setSingleShot(true);connect(&timeout_,&QTimer::timeout,this,[this]{fail("Update preparation timed out; installed Viewer unchanged");});
}
Installer::~Installer(){
  if(!handoff_&&!work_.isEmpty()){QFile cancel(work_+"/cancelled");if(cancel.open(QIODevice::WriteOnly))cancel.write("cancelled");}
  if(process_.state()!=QProcess::NotRunning){process_.kill();process_.waitForFinished(3000);}
}
void Installer::fail(const QString& message){
  if(failed_)return;failed_=true;timeout_.stop();process_.disconnect(this);
  if(!work_.isEmpty()){QFile cancel(work_+"/cancelled");if(cancel.open(QIODevice::WriteOnly))cancel.write("cancelled");}
  if(process_.state()!=QProcess::NotRunning)process_.kill();result_(message);
}
void Installer::run(const QString& command,const QStringList& args,std::function<void()> next){
  process_.disconnect(this);process_.setReadChannel(QProcess::StandardOutput);
  connect(&process_,&QProcess::readyReadStandardOutput,this,[this]{if(process_.bytesAvailable()>4*1024*1024)fail("Archive listing too large");});
  connect(&process_,&QProcess::errorOccurred,this,[this](QProcess::ProcessError e){if(e==QProcess::FailedToStart)fail(process_.errorString());});
  connect(&process_,QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),this,[this,next](int code,QProcess::ExitStatus status){
    timeout_.stop();if(failed_)return;
    if(code||status!=QProcess::NormalExit){fail("Cannot prepare update: "+QString::fromUtf8(process_.readAllStandardError()).left(2000));return;}
    try{next();}catch(const std::exception& e){fail(QString::fromUtf8(e.what()));}
  });
  timeout_.start(300000);process_.start(command,args);
}
void Installer::start(const QString& archive,const Release& release){try{
  need(idle_(),"Stop capture/recording and close the device connection before installing");
  QFile f(archive);need(f.open(QIODevice::ReadOnly)&&f.size()==release.size,"Update package size changed");
  QCryptographicHash hash(QCryptographicHash::Sha256);need(hash.addData(&f)&&hash.result()==release.sha256,"Update package checksum changed");f.close();
  QDir location(QCoreApplication::applicationDirPath());
#if defined(Q_OS_MACOS)
  need(location.cdUp()&&location.cdUp(),"Invalid application bundle");target_=location.absolutePath();
  need(target_.endsWith(".app")&&QFile::exists(target_+"/Contents/Info.plist"),"Run the installed application bundle");
  root_="Prism-Viewer.app";relative_="Contents/MacOS/Prism-Viewer";
#elif defined(Q_OS_WIN)
  target_=location.absolutePath();root_="Prism-Viewer-"+release.version+"-windows-x64";relative_="prism-viewer.exe";
  need(QFile::exists(target_+"/prism_usb_sdk.dll")&&QFile::exists(target_+"/SHA256SUMS.txt"),"Run the complete portable Viewer package");
#else
  need(location.dirName()=="bin"&&location.cdUp(),"Run the complete portable Viewer package");
  target_=location.absolutePath();root_="Prism-Viewer-"+release.version+"-"+platform();relative_="bin/prism-viewer";
  need(QFile::exists(target_+"/SHA256SUMS.txt"),"Run the complete portable Viewer package");
#endif
  need(QFileInfo(target_).canonicalFilePath()==target_,"Do not update through a symbolic link");
  const auto parent=QFileInfo(target_).absolutePath();
  need(parent!="/"&&target_!=QDir::homePath(),"Invalid application location");
  const QStorageInfo space(parent);need(!space.isReadOnly()&&space.bytesAvailable()>9LL*1024*1024*1024,"Update needs a writable application folder and 9 GiB free space");
  QTemporaryDir work(parent+"/.prism-viewer-update-XXXXXX");need(work.isValid(),"Application folder is not writable");
  work.setAutoRemove(false);work_=work.path();need(QDir().mkdir(work_+"/extract"),"Cannot create extraction directory");staged_=work_+"/extract/"+root_;
#if defined(Q_OS_WIN)
  const auto helper=script(work_,"extract.ps1");
  run(powershell(),{"-NoProfile","-NonInteractive","-ExecutionPolicy","Bypass","-File",helper,archive,work_+"/extract",root_},[this]{extracted();});
#else
  const auto listing=platform()=="macos-arm64"?QString("/usr/bin/unzip"):QString("/usr/bin/tar");
  const QStringList args=platform()=="macos-arm64"?QStringList{"-Z1",archive}:QStringList{"-tzf",archive};
  run(listing,args,[this,archive]{
    const auto lines=process_.readAllStandardOutput().split('\n');need(lines.size()<100000,"Too many archive entries");
    for(const auto& bytes:lines){if(bytes.isEmpty())continue;const auto name=QString::fromUtf8(bytes);
      need(!name.startsWith('/')&&!name.contains('\\')&&!name.split('/').contains(".."),"Unsafe archive path");
      need(name==root_||name.startsWith(root_+"/")||(platform()=="macos-arm64"&&name.startsWith("__MACOSX/")),"Unexpected package root");
    }
    if(platform()=="macos-arm64")run("/usr/bin/ditto",{"-x","-k",archive,work_+"/extract"},[this]{extracted();});
    else run("/usr/bin/tar",{"--no-same-owner","-xzf",archive,"-C",work_+"/extract"},[this]{extracted();});
  });
#endif
}catch(const std::exception& e){fail(QString::fromUtf8(e.what()));}}
void Installer::extracted(){
  qint64 size=0;unsigned count=0;const auto canonical=QFileInfo(staged_).canonicalFilePath();need(!canonical.isEmpty(),"Package root missing");
  QDirIterator it(staged_,QDir::Files|QDir::Dirs|QDir::Hidden|QDir::NoDotAndDotDot,QDirIterator::Subdirectories);
  while(it.hasNext()){
    it.next();const auto info=it.fileInfo();const auto resolved=info.canonicalFilePath();
    need(resolved==canonical||resolved.startsWith(canonical+"/"),"Package symlink escapes its root");
    size+=info.isFile()?info.size():0;need(++count<=100000&&size<=8LL*1024*1024*1024,"Extracted package exceeds limit");
  }
  need(QFileInfo(staged_+"/"+relative_).isExecutable(),"Updated executable missing");
  need(idle_(),"Device became active; update prepared but Viewer was not closed");
  const auto backup=target_+".backup-"+QFileInfo(work_).fileName().mid(21);
  need(!QFileInfo::exists(backup),"Backup path already exists");
  QString program;QStringList arguments;
#if defined(Q_OS_WIN)
  program=powershell();arguments=QStringList{"-NoProfile","-NonInteractive","-ExecutionPolicy","Bypass","-File",script(work_,"install.ps1")};
#else
  program="/bin/sh";arguments=QStringList{script(work_,"install.sh")};
#endif
  arguments<<QString::number(QCoreApplication::applicationPid())<<target_<<staged_<<backup<<relative_<<work_;
  need(QProcess::startDetached(program,arguments,work_),"Cannot start update helper");
  auto* poll=new QTimer(this);poll->setInterval(100);auto attempts=std::make_shared<int>(0);
  connect(poll,&QTimer::timeout,this,[this,poll,attempts]{
    if(QFile::exists(work_+"/ready")){
      poll->stop();if(!idle_()){fail("Device became active; Viewer was not closed");return;}
      handoff_=true;result_("Installing update; Viewer will restart. Previous package retained at its backup path.");
      QCoreApplication::quit();
    }else if(++*attempts>=50){poll->stop();fail("Update helper did not become ready; Viewer unchanged");}
  });poll->start();
}
}
