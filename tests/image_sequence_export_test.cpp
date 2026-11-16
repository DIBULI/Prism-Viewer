#include "dataset/image_sequence_exporter.hpp"
#include "common/ui_text.hpp"
#include "raw_dataset_fixture.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QProcess>
#include <iostream>
using namespace prism_viewer::dataset;
namespace fs=std::filesystem;
void check(bool ok,const std::string& text){if(!ok)throw std::runtime_error(text);}
QByteArray readFile(const fs::path& p){QFile f(prism_viewer::common::fromFilesystemPath(p));check(f.open(QIODevice::ReadOnly),"read");return f.readAll();}
QByteArray jpeg(){QImage im(1280,1024,QImage::Format_RGB32);im.fill(QColor(40,90,120));QByteArray b;QBuffer f(&b);f.open(QIODevice::WriteOnly);check(im.save(&f,"JPEG"),"jpeg fixture");return b;}
std::vector<uint8_t> mixedFrame(unsigned c){
  constexpr uint32_t id=7;constexpr uint64_t time=1234567;
  if(c==1||c==2){
    std::vector<uint8_t> pixels(4096*3072);
    for(size_t i=0;i<pixels.size();++i)pixels[i]=uint8_t(32+(i/256)%192);
    auto b=native8_fixture::header(c,id,time,prism::capture::rawCrc32(pixels.data(),pixels.size()));
    native8_fixture::put(b,8,4096);native8_fixture::put(b,16,uint32_t(pixels.size()));
    native8_fixture::put(b,64,uint32_t(pixels.size()/256));native8_fixture::put(b,68,3);native8_fixture::put(b,72,6000);
    b[38]=4;native8_fixture::put(b,52,id);
    for(size_t i=0;i<pixels.size();i+=256)b.insert(b.end(),{0,pixels[i],pixels[i]});
    native8_fixture::seal(b);return b;
  }
  const auto original=jpeg();std::vector<uint8_t> h(64);
  std::memcpy(h.data(),"PRJ1",4);h[4]=2;h[5]=64;h[28]=uint8_t(c);h[29]=1;h[30]=4;
  native8_fixture::put(h,8,1280);native8_fixture::put(h,12,1024);native8_fixture::put(h,16,id);
  native8_fixture::put(h,20,1200);native8_fixture::put(h,24,2048);native8_fixture::put(h,32,uint32_t(time));
  native8_fixture::put(h,40,prism::capture::rawCrc32(reinterpret_cast<const uint8_t*>(original.data()),original.size()));
  native8_fixture::put(h,44,uint32_t(original.size()));native8_fixture::put(h,48,id);
  native8_fixture::put(h,60,prism::capture::rawCrc32(h.data(),60));
  std::vector<uint8_t> b={255,216,255,239,0,66};b.insert(b.end(),h.begin(),h.end());b.insert(b.end(),original.begin()+2,original.end());return b;
}
void mixed(const fs::path& p){
  fs::create_directory(p);std::string info="format=prism-mixed-dataset-v1\ndevice_model=21312V0\ndevice_serial=test-serial\ncodec=per-camera-jpeg-and-raw8-rice\ncamera_count=4\ncomplete=1\n";
  for(unsigned c=0;c<4;++c){
    auto b=mixedFrame(c);const auto prefix="camera"+std::to_string(c);const bool raw=c==1||c==2;
    raw_fixture::save(p/(prefix+(raw?".pr8-stream":".mjpg-stream")),std::string(b.begin(),b.end()));
    QJsonObject row{{"offset",0},{"bytes",int(b.size())},{"frame_id",7},{"timestamp_us",1234567},{"timing_mode",4},{"trigger_sequence",7},
      {"raw_crc32",double(prism::capture::rawLe32(b.data()+(raw?20:46)))},{"encoded_crc32",double(raw?prism::capture::rawLe32(b.data()+48):prism::capture::rawCrc32(b.data(),b.size()))},{"dropped_before",0}};
    raw_fixture::save(p/(prefix+".jsonl"),QJsonDocument(row).toJson(QJsonDocument::Compact).toStdString()+"\n");
    info+=prefix+"_images=1\n";
  }
  raw_fixture::save(p/"dataset.info",info);
}
int main(int argc,char** argv){
  QCoreApplication app(argc,argv);
  try{
    if(argc==3&&std::string(argv[1])=="--ui") {
      QTemporaryDir temp;check(temp.isValid(),"UI temp");
      auto r=prism_viewer::common::toFilesystemPath(temp.path());mixed(r/"mixed");
      raw_fixture::create(r/"raw415",true,true,4);
      fs::create_directory(r/"partial413");
      auto j=jpeg();raw_fixture::save(r/"partial413/frames.bin",j.toStdString());
      raw_fixture::save(r/"partial413/dataset.info","format=prism-dataset-v6\ndevice_model=413GSV0\ncomplete=1\n");
      raw_fixture::save(r/"partial413/cam0.tum","0.000000 frames.bin 0 "+std::to_string(j.size())+" 1000\n");
      for(const auto* name:{"mixed","raw415","partial413"}) {
        QProcess process;process.start(QString::fromLocal8Bit(argv[2]),{"--image-export-self-test",prism_viewer::common::fromFilesystemPath(r/name),temp.path()});
        check(process.waitForFinished(60000),"UI export timeout");
        std::cout<<name<<": "<<process.readAllStandardOutput().toStdString()<<process.readAllStandardError().toStdString();
        check(process.exitStatus()==QProcess::NormalExit&&process.exitCode()==0,"UI export failed");
      }
      check(readFile(r/"partial413-images/camera0/00000000_0.jpg")==j,"partial413 UI JPEG mismatch");
      check(fs::exists(r/"mixed-images/camera1/frames.csv"),"UI did not export camera index");return 0;
    }
    if(argc==3){
      const auto input=prism_viewer::common::toFilesystemPath(QString::fromLocal8Bit(argv[1]));
      const auto output=prism_viewer::common::toFilesystemPath(QString::fromLocal8Bit(argv[2]));
      auto result=exportImageSequence(input,output);check(result.success,result.error);
      auto index=inspectImageSequence(input);
      for(unsigned c=0;c<4;++c){unsigned ordinal=0;for(auto& e:index.cameras[c]){
        std::ostringstream filename;filename<<std::setw(8)<<std::setfill('0')<<ordinal++<<'_'<<e.timestamp_us<<(e.raw8?".png":".jpg");
        QFile f(e.absolute_path);check(f.open(QIODevice::ReadOnly)&&f.seek(e.byte_offset),"source read");auto bytes=f.read(e.byte_size);
        auto out=output/("camera"+std::to_string(c))/filename.str();
        if(e.raw8){
          const auto raw=prism::capture::decodeRawImage(std::vector<uint8_t>(bytes.begin(),bytes.end()));
          QImage png(prism_viewer::common::fromFilesystemPath(out));check(png.size()==QSize(raw.width,raw.height),"real PNG geometry");
          for(unsigned y=0;y<raw.height;++y)for(unsigned x=0;x<raw.width;++x)
            check(qGray(png.pixel(x,y))==prism::capture::rawPixel(raw,size_t(y)*raw.width+x),"real PNG pixel mismatch");
        }else check(readFile(out)==bytes,"real JPEG byte mismatch");
      }}
      for(auto n:result.images)std::cout<<n<<' ';std::cout<<"PASS real JPEG bytes and all PNG pixels\n";return 0;
    }
    QTemporaryDir tmp;check(tmp.isValid(),"temp");auto root=prism_viewer::common::toFilesystemPath(tmp.path());
    auto legacy=root/"413";fs::create_directory(legacy);auto jpg=jpeg();
    raw_fixture::save(legacy/"dataset.info","format=prism-dataset-v6\ndevice_model=413GSV0\ncomplete=1\ntime_domain=unknown\n");
    for(unsigned c=0;c<4;++c){raw_fixture::save(legacy/("cam"+std::to_string(c)+".tum"),"0.000000 frames.bin 0 "+std::to_string(jpg.size())+" 1000\n");}
    raw_fixture::save(legacy/"frames.bin",jpg.toStdString());
    auto a=exportImageSequence(legacy,root/"413-images");check(a.success,a.error);check(a.images==std::array<uint64_t,4>{1,1,1,1},"413 counts");
    check(readFile(root/"413-images/camera0/00000000_0.jpg")==jpg,"JPEG bytes changed");
    check(readFile(root/"413-images/camera0/frames.csv").contains(",0,1000,,,,,,"),"unknown metadata fabricated");
    check(!exportImageSequence(legacy,root/"413-images").success,"overwrite permitted");check(!exportImageSequence(legacy,legacy/"out").success,"source nested export permitted");
    unsigned done=0;auto cancelled=exportImageSequence(legacy,root/"cancelled",[&](uint64_t n,const std::string&){done=unsigned(n);},[&]{return done==1;});
    check(cancelled.cancelled&&!fs::exists(root/"cancelled"),"cancel published partial output");
    for(auto& e:fs::directory_iterator(root))check(e.path().filename().string().find(".prism-image-export-")!=0,"staging leaked");
    raw_fixture::save(legacy/"cam0.tum","1.000000 ../escape.jpg 0 100 1000\n");raw_fixture::save(root/"escape.jpg",jpg.toStdString());
    check(!exportImageSequence(legacy,root/"escaped").success,"path escape accepted");
    raw_fixture::save(legacy/"cam0.tum","1.000000 frames.bin 999999 100 1000\n");check(!exportImageSequence(legacy,root/"truncated").success,"invalid offset accepted");
    auto raw=root/"415";raw_fixture::create(raw,true,true,4);
    auto b=exportImageSequence(raw,root/"415-images");check(b.success,b.error);check(b.images==std::array<uint64_t,4>{2,0,2,3},"415 drop/absent counts");
    QImage rawPng(prism_viewer::common::fromFilesystemPath(root/"415-images/camera0/00000000_2000000.png"));check(!rawPng.isNull()&&rawPng.size()==QSize(5120,3072),"415 PNG size");
    for(int y=0;y<rawPng.height();++y)for(int x=0;x<rawPng.width();++x)check(qGray(rawPng.pixel(x,y))==0x83,"415 pixel changed");
    mixed(root/"21312");auto c=exportImageSequence(root/"21312",root/"21312-images");check(c.success,c.error);
    check(readFile(root/"21312-images/camera0/00000000_1234567.jpg")==readFile(root/"21312/camera0.mjpg-stream"),"21312 JPEG changed");
    QImage png(prism_viewer::common::fromFilesystemPath(root/"21312-images/camera1/00000000_1234567.png"));
    check(png.size()==QSize(4096,3072),"21312 PNG size");
    for(int y=0;y<png.height();++y)for(int x=0;x<png.width();++x)check(qGray(png.pixel(x,y))==32+((size_t(y)*4096+x)/256)%192,"21312 Bayer pixel changed");
    check(readFile(root/"21312-images/export.json").contains("test-serial"),"serial omitted");
    check(readFile(root/"21312-images/camera1/frames.csv").contains(",1234567,1000,2048,7,7,4,0,4096,3072,Bayer8,"),"RAW metadata lost");
    auto info=readFile(root/"21312/dataset.info");info.replace("complete=1","complete=0");raw_fixture::save(root/"21312/dataset.info",info.toStdString());
    check(!exportImageSequence(root/"21312",root/"incomplete").success,"active recording accepted");
    info.replace("complete=0","complete=1");raw_fixture::save(root/"21312/dataset.info",info.toStdString());
    {std::fstream f(root/"21312/camera1.pr8-stream",std::ios::in|std::ios::out|std::ios::binary);f.seekp(81);f.put(0);}
    check(!exportImageSequence(root/"21312",root/"corrupt").success&&!fs::exists(root/"corrupt"),"corrupt RAW published");
    std::cout<<"PASS 413/JPEG exact;415/21312 PNG all pixels;metadata;cancel;bounds;corruption;source protection\n";
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
