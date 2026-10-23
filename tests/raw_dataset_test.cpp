#include "dataset/raw12_dataset.hpp"
#include "common/ui_text.hpp"
#include "raw_dataset_fixture.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QProcess>
#include <iostream>
using namespace prism_viewer::dataset;
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
int main(int argc,char** argv){
  QCoreApplication app(argc,argv);
  try{
    if(argc==3 && std::string(argv[1])=="--ui") {
      QTemporaryDir fixture; check(fixture.isValid(),"UI fixture temp directory");
      const auto root=prism_viewer::common::toFilesystemPath(fixture.path());
      raw_fixture::create(root,true,true);
      QProcess process; process.start(QString::fromLocal8Bit(argv[2]), {"--dataset-playback-self-test",fixture.path()});
      check(process.waitForFinished(60000),"UI test timed out");
      std::cout<<process.readAllStandardOutput().toStdString()<<process.readAllStandardError().toStdString();
      check(process.exitStatus()==QProcess::NormalExit&&process.exitCode()==0,"UI replay test failed");return 0;
    }
    if(argc==3 && std::string(argv[1])=="--create") {
      const auto root=prism_viewer::common::toFilesystemPath(QString::fromLocal8Bit(argv[2]));
      check(!std::filesystem::exists(root),"fixture target must not exist");
      raw_fixture::create(root,true,true);return 0;
    }
    if(argc==2){
      const auto root=prism_viewer::common::toFilesystemPath(QString::fromLocal8Bit(argv[1]));
      const auto validation=validateRaw12Dataset(root,{},{});
      for(const auto& i:validation.issues)std::cout<<i.file<<": "<<i.message<<'\n';
      check(validation.valid,"real dataset validation failed");
      const auto data=loadRaw12Dataset(root);
      for(size_t c=0;c<4;++c){std::cout<<"camera"<<c<<"="<<data.cameras[c].size()<<'\n';
        if(!data.cameras[c].empty())check(!loadRaw12DatasetImage(data.cameras[c].back()).isNull(),"real tail decode");}
      std::cout<<"PASS real frames="<<validation.checked_records<<" steps="<<data.timestamps.size()<<'\n';return 0;
    }
    QTemporaryDir dir;check(dir.isValid(),"temp dir");
    const auto root=prism_viewer::common::toFilesystemPath(dir.path());
    auto info=raw_fixture::create(root);
    auto data=loadRaw12Dataset(root);
    check(data.cameras[3].size()==3 && data.timestamps.size()==9,"unequal counts lost");
    check(data.display.front()[0]==0 && data.display.front()[1]==-1,"future sample shown");
    check(data.display.back()[3]==2,"tail frame omitted");
    auto image=loadRaw12DatasetImage(data.cameras[0][0]);
    check(image.size()==QSize(5120,3072)&&image.constScanLine(0)[0]==0x83,"grayscale mapping");
    auto v=validateRaw12Dataset(root,{},{});check(v.valid&&v.checked_records==9,"full validation");
    check(validateRaw12Dataset(root,{},[]{return true;}).cancelled,"cancel");
    // Text manifests only for new writers; early on-device JSON still replays.
    QJsonObject legacy{{"schema",1},{"model","Prism-415M"},{"codec","raw12-rice-lossless"},
        {"complete",true},{"width",5120},{"height",3072},{"source_bits",12},{"camera_count",4},
        {"fps_per_camera",1},{"bytes",double(data.bytes)},{"frames",QJsonArray{2,2,2,3}}};
    raw_fixture::save(root/"dataset.info",QJsonDocument(legacy).toJson().toStdString());
    check(loadRaw12Dataset(root).timestamps==data.timestamps,"legacy JSON");
    auto rejects=[&]{try{(void)loadRaw12Dataset(root);}catch(const std::exception&){return true;}return false;};
    raw_fixture::save(root/"dataset.info",info+"complete=0\n");check(rejects(),"duplicate metadata");
    auto incomplete=info;incomplete.replace(incomplete.find("complete=1"),10,"complete=0");
    raw_fixture::save(root/"dataset.info",incomplete);check(rejects(),"incomplete accepted");
    raw_fixture::save(root/"dataset.info",info);
    {std::fstream f(root/"camera0.prg-stream",std::ios::binary|std::ios::in|std::ios::out);f.seekp(70);f.put(1);}
    check(!validateRaw12Dataset(root,{},{}).valid,"encoded corruption accepted");
    bool crc=false;try{loadRaw12DatasetImage(data.cameras[0][0]);}catch(const std::exception&){crc=true;}check(crc,"display CRC");
    raw_fixture::create(root);
    // Match a deliberately wrong decoded CRC in the header and JSON index.
    // Structural checks must pass, but full pixel verification must reject it.
    {
      QFile index(QString::fromStdString((root/"camera0.jsonl").string()));
      check(index.open(QIODevice::ReadOnly),"index read");auto lines=index.readAll().split('\n');index.close();
      auto row=QJsonDocument::fromJson(lines[0]).object();const uint32_t wrong=uint32_t(row["raw_crc32"].toDouble())^1;
      row["raw_crc32"]=double(wrong);lines[0]=QJsonDocument(row).toJson(QJsonDocument::Compact);
      raw_fixture::save(root/"camera0.jsonl",lines.join('\n').toStdString());
      std::fstream f(root/"camera0.prg-stream",std::ios::binary|std::ios::in|std::ios::out);f.seekp(20);
      for(unsigned i=0;i<4;++i)f.put(char(wrong>>(8*i)));
    }
    check(loadRaw12Dataset(root).cameras[0].size()==2,"decoded CRC fixture structure");
    check(!validateRaw12Dataset(root,{},{}).valid,"decoded pixel corruption accepted");
    info=raw_fixture::create(root,true,true);data=loadRaw12Dataset(root);
    check(data.dropped[0]==3&&data.dropped[1]==2&&data.cameras[1].empty(),"drop accounting");
    check(data.display.back()[2]==-1,"stale stream retained");
    v=validateRaw12Dataset(root,{},{});check(v.valid&&v.checked_records==7&&v.total_warnings==3,"drop validation");
    auto bad=info;const auto pos=bad.find("camera0_dropped_tail=1");bad.replace(pos,22,"camera0_dropped_tail=2");
    raw_fixture::save(root/"dataset.info",bad);check(rejects(),"tail drop mismatch accepted");
    raw_fixture::save(root/"dataset.info",info);
    raw_fixture::save(root/"camera2.jsonl","{\"offset\":-1}\n");check(rejects(),"negative/missing index accepted");
    raw_fixture::create(root);
    {std::ofstream f(root/"camera3.prg-stream",std::ios::app|std::ios::binary);f.put(0);}
    check(rejects(),"trailing data accepted");
    std::cout<<"PASS RAW12 text/legacy, counts/timeline, grayscale, full CRC, drops, cancellation, corruption/bounds\n";
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
