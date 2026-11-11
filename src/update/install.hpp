#pragma once
#include "update/release.hpp"
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <functional>
namespace prism_viewer::update {
class Installer final:public QObject {
 public:
  Installer(QObject* owner,std::function<bool()> idle,std::function<void(QString)> result);
  ~Installer() override;
  void start(const QString& archive,const Release& release);
 private:
  void run(const QString&,const QStringList&,std::function<void()>);
  void extracted();void fail(const QString&);
  std::function<bool()> idle_;
  std::function<void(QString)> result_;
  QProcess process_;
  QTimer timeout_;
  QString target_,work_,stageWork_,staged_,relative_,root_;
  bool failed_=false;
  bool handoff_=false;
};
}
