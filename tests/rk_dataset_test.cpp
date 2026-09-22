#include "dataset/rk_dataset_client.hpp"
#include "dataset/dataset_browser.hpp"
#include "dataset/rosbag_exporter.hpp"
#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QProcess>
#include <QtCore/QTemporaryDir>
#include <QtCore/QVariant>
#include <QtGui/QImage>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>
#include <filesystem>
#include <iostream>
#include <map>

using namespace prism_viewer::dataset;
void require(bool value, const char* message) { if(!value) throw std::runtime_error(message); }
template<class Function> void rejects(Function f) {
  try { f(); } catch(const std::exception&) { return; }
  throw std::runtime_error("expected failure");
}
QByteArray read(const QString& path) { QFile file(path); require(file.open(QIODevice::ReadOnly),"read failed");return file.readAll(); }
std::filesystem::path fsPath(const QString& path) { return std::filesystem::u8path(path.toUtf8().constData()); }

QByteArray headerValue(const QByteArray& request, const QByteArray& name) {
  // HTTP field names are case-insensitive. Qt 6 serializes some names in
  // lowercase; the opaque ETag value must still be compared byte-for-byte.
  for (const auto& line : request.split('\n')) {
    const int colon = line.indexOf(':');
    if (colon > 0 && line.left(colon).trimmed().compare(name, Qt::CaseInsensitive) == 0)
      return line.mid(colon + 1).trimmed();
  }
  return {};
}

void fixture(const QString& worker, const QString& root, const QString& epoch) {
  QImage image(16,16,QImage::Format_RGB32); image.fill(Qt::green);
  const QString jpeg=root+QStringLiteral("/fixture.jpg"); require(image.save(jpeg,"JPG"),"JPEG fixture failed");
  const QString dataset=root+QStringLiteral("/capture-")+epoch;
  QProcess process; process.start(worker,{QStringLiteral("--viewer-fixture"),dataset,jpeg,epoch});
  require(process.waitForFinished(30000),"recorder fixture timeout");
  if(process.exitCode()!=0) throw std::runtime_error(process.readAllStandardError().constData());
  auto validation=validatePrismDataset(fsPath(dataset));
  for(const auto& issue:validation.issues) std::cerr<<issue.file<<": "<<issue.message<<'\n';
  require(validation.valid,"RK recording must validate as a native Viewer dataset");
  require(validation.cameras[0].rows==3 && validation.onboard_imus[0].rows==3 &&
          validation.lidar.rows==3 && validation.lidar_imu.rows==3,"RK stream counts differ");
  require(validation.timestamp_epoch==(epoch==QStringLiteral("utc")?"unix":"boot"),"timestamp epoch changed");
  require(read(dataset+QStringLiteral("/dataset.info")).contains("unsynced_lidar_batches_skipped=1"),"unsynced lidar not counted");
  require(read(dataset+QStringLiteral("/events.csv")).count('\n')==9,"raw unsynced events were lost");
  for(auto format:{RosbagFormat::Ros1,RosbagFormat::Ros2}) {
    const auto output=dataset+(format==RosbagFormat::Ros1?QStringLiteral(".bag"):QStringLiteral(".rosbag2"));
    auto bag=exportDatasetToRosbag(fsPath(dataset),fsPath(output),format,false);
    if(!bag.success) throw std::runtime_error(bag.error);
    require(bag.camera_messages==12 && bag.camera_exposure_messages==12 && bag.imu_messages==3 &&
            bag.lidar_points==6 && bag.lidar_imu_messages==3 && bag.lidar_messages>0,"bag lost a sensor stream");
  }
  std::cout<<"PASS RK writer -> Viewer v6 validation -> ROS1/ROS2, epoch="<<epoch.toStdString()<<'\n';
}

int main(int argc,char** argv) {
  QCoreApplication app(argc,argv);
  try {
    // Optional read-only verification/export of a real downloaded recording.
    if(argc==4 && std::string(argv[1])=="--export") {
      const auto root=std::filesystem::u8path(argv[2]);
      auto valid=validatePrismDataset(root);
      for(const auto& issue:valid.issues) std::cerr<<issue.file<<": "<<issue.message<<'\n';
      require(valid.valid,"real recording failed validation");
      for(auto format:{RosbagFormat::Ros1,RosbagFormat::Ros2}) {
        auto bag=exportDatasetToRosbag(root,std::filesystem::u8path(std::string(argv[3])+(format==RosbagFormat::Ros1?".bag":".rosbag2")),format,false);
        if(!bag.success) throw std::runtime_error(bag.error);
        std::cout<<"PASS real bag: camera="<<bag.camera_messages<<" imu="<<bag.imu_messages
                 <<" lidar_points="<<bag.lidar_points<<" lidar_imu="<<bag.lidar_imu_messages<<'\n';
      }
      return 0;
    }
    if(argc==5 && std::string(argv[1])=="--download") {
      std::cout<<downloadRkDataset(rkDatasetEndpoint(QString::fromUtf8(argv[2])),QString::fromUtf8(argv[3]),QString::fromUtf8(argv[4])).toStdString()<<'\n';return 0;
    }
    require(headerValue("GET / HTTP/1.1\r\nif-match: \"V1\"\r\n\r\n", "If-Match") == "\"V1\"",
            "header parser must ignore field-name case but preserve ETag case");
    require(rkDatasetEndpoint(QStringLiteral("10.42.200.1")).port()==80,"default port");
    require(rkDatasetEndpoint(QStringLiteral("http://10.42.200.1:8080")).port()==8080,"explicit legacy port");
    for(const auto& address:{"file:///etc/passwd","http://example.com:8080","http://user:pass@127.0.0.1/","http://127.0.0.1/foo","http://127.0.0.1/?x=1"})
      rejects([&]{rkDatasetEndpoint(QString::fromLatin1(address));});
    QTemporaryDir temp; require(temp.isValid(),"temporary directory");
    QTcpServer server; require(server.listen(QHostAddress::LocalHost,0),"mock listen");
    const QUrl base(QStringLiteral("http://127.0.0.1:%1/").arg(server.serverPort()));
    int mode=0, inventory_requests=0;
    const QByteArray blob(1024*1024,'x');
    const QByteArray manifest("{\"format\":\"prism-web-dataset-v1\",\"complete\":true}");
    std::map<QString,QByteArray> files{{QStringLiteral("manifest.json"),manifest},{QStringLiteral("camera-data-0000.bin"),blob}};
    QObject::connect(&server,&QTcpServer::newConnection,&app,[&] {
      while(server.hasPendingConnections()) {
        auto* socket=server.nextPendingConnection();
        QObject::connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
        QObject::connect(socket,&QTcpSocket::readyRead,socket,[&,socket] {
          auto input=socket->property("request").toByteArray()+socket->readAll(); socket->setProperty("request",input);
          if(!input.contains("\r\n\r\n") || socket->property("answered").toBool()) return;
          socket->setProperty("answered",true);
          const auto parts=input.split(' ');const QByteArray path=parts.value(1);
          QByteArray body,extra;int status=200;
          if(path=="/api/datasets") {
            body="[{\"name\":\"capture-test\",\"manifest\":{\"complete\":true}}]";
          } else if(path=="/api/datasets/capture-test/files") {
            ++inventory_requests;QJsonArray entries;quint64 total=0;
            for(const auto& file:files) { entries.append(QJsonObject{{"name",file.first},{"size",double(file.second.size())},{"etag",QStringLiteral("\"v1\"")}});total+=file.second.size(); }
            if(mode==1) entries.append(QJsonObject{{"name","../outside"},{"size",0},{"etag","\"v1\""}});
            if(mode==4 && inventory_requests>1) entries[0]=QJsonObject{{"name","camera-data-0000.bin"},{"size",double(blob.size())},{"etag","\"changed\""}};
            body=QJsonDocument(QJsonObject{{"name","capture-test"},{"format","prism-web-dataset-v1"},{"files",entries},{"total_bytes",double(total)}}).toJson(QJsonDocument::Compact);
          } else {
            const auto filename=QString::fromLatin1(path.mid(path.lastIndexOf('/')+1));
            if(!files.count(filename)) status=404;
            else { body=files.at(filename);extra="ETag: \"v1\"\r\n";require(headerValue(input,"If-Match")=="\"v1\"","download missing snapshot condition"); }
          }
          if(mode==2) {status=302;extra="Location: http://127.0.0.1:9/secret\r\n";body.clear();}
          const auto size=body.size();
          if(mode==3 && path.endsWith(".bin")) body.chop(1);
          socket->write("HTTP/1.1 "+QByteArray::number(status)+" Test\r\nContent-Length: "+QByteArray::number(size)+"\r\nConnection: close\r\n"+extra+"\r\n"+body);
          socket->disconnectFromHost();
        });
      }
    });
    require(listRkDatasets(base).size()==1,"list failed");
    const auto path=downloadRkDataset(base,QStringLiteral("capture-test"),temp.path());
    require(read(path+QStringLiteral("/camera-data-0000.bin"))==blob && read(path+QStringLiteral("/manifest.json"))==manifest,"download changed original bytes");
    rejects([&]{downloadRkDataset(base,QStringLiteral("capture-test"),temp.path());});
    for(mode=1;mode<=4;++mode) {
      QTemporaryDir target;inventory_requests=0;
      rejects([&]{downloadRkDataset(base,QStringLiteral("capture-test"),target.path());});
      require(!QFileInfo::exists(target.path()+QStringLiteral("/capture-test")),"failed download advertised complete");
    }
    mode=0;QTemporaryDir cancelled;bool stop=false;
    rejects([&]{downloadRkDataset(base,QStringLiteral("capture-test"),cancelled.path(),[&](quint64 n,quint64,const QString&){if(n)stop=true;},[&]{return stop;});});
    require(!QFileInfo::exists(cancelled.path()+QStringLiteral("/capture-test")),"cancel advertised complete");
    std::cout<<"PASS RK download: byte equality, no overwrite, traversal/redirect rejection, truncation, changed snapshot, cancellation\n";
    const QString worker=qEnvironmentVariable("PRISM_WEB_WORKER");
    if(!worker.isEmpty()) {fixture(worker,temp.path(),QStringLiteral("boot"));fixture(worker,temp.path(),QStringLiteral("utc"));}
    else std::cout<<"SKIP external recorder fixtures (set PRISM_WEB_WORKER)\n";
    return 0;
  } catch(const std::exception& error) {std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
