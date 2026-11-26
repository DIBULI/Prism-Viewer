#include <filesystem>
#include <fstream>
#include <string>
#include <QCoreApplication>
#include <QTimer>
#include "update/cleanup.hpp"
int main(int argc,char** argv){
  QCoreApplication app(argc,argv);
  const auto root=std::filesystem::absolute(argv[0]).parent_path();
  if(argc==4&&std::string(argv[1])=="--cleanup-update-backup")
    return prism_viewer::update::cleanupPreviousPackage(QCoreApplication::applicationDirPath(),app.arguments()[2],app.arguments()[3]);
  if(argc==3&&std::string(argv[1])=="--finish-update-cleanup")
    return prism_viewer::update::finishUpdateCleanup(QCoreApplication::applicationDirPath(),app.arguments()[2]);
  if(argc==3&&std::string(argv[1])=="--startup-cleanup-fixture") {
    prism_viewer::update::scheduleUpdateCleanup(&app,app.arguments()[2]);
    QTimer::singleShot(4000,&app,&QCoreApplication::quit);
    return app.exec();
  }
  if(std::filesystem::exists(root/"fail-start"))return 9;
  if(argc==3&&std::string(argv[1])=="--viewer-update-ack")std::ofstream(argv[2])<<"started";
  else std::ofstream(root/"started-old")<<"started";
  return 0;
}
