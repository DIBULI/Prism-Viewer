#include "ui/raw_preview.hpp"
#include "ui/raw_preview_group.hpp"
#include "native8_fixture.hpp"
#include <QCoreApplication>
#include <iostream>
void check(bool v,const char* text){if(!v)throw std::runtime_error(text);}
QImage referenceThumbnail(const std::vector<uint8_t>& pixels,QSize size){
  QImage cells(2560,1536,QImage::Format_Grayscale8);
  for(unsigned y=0;y<1536;++y)for(unsigned x=0;x<2560;++x){
    const size_t at=size_t(y*2)*5120+x*2;
    cells.scanLine(y)[x]=uint8_t((unsigned(pixels[at])+pixels[at+1]+pixels[at+5120]+pixels[at+5121])/4);
  }
  return cells.scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation).convertToFormat(QImage::Format_Grayscale8);
}
int main(int argc,char**argv){QCoreApplication app(argc,argv);try{
  using namespace prism_viewer::ui;using namespace native8_fixture;
  constexpr auto format=prism::capture::kRaw8Rice;
  std::vector<uint8_t> pixels(prism::capture::kRaw8Bytes);
  for(size_t i=0;i<pixels.size();++i)pixels[i]=uint8_t(i);
  auto wire=verbatim(pixels);const auto original=wire;
  auto full=decodeRawPreview(wire,{},2,format);
  check(full.size()==QSize(5120,3072)&&full.format()==QImage::Format_Grayscale8,"native geometry");
  for(unsigned y=0;y<3072;++y)check(!std::memcmp(full.constScanLine(y),pixels.data()+size_t(y)*5120,5120),"native byte altered");
  check(isNativeBayerPreview(full),"native provenance");
  for(QSize maximum:{QSize(640,512),QSize(320,192),QSize(233,140),QSize(4000,2400)}){
    const auto size=QSize(5120,3072).scaled(maximum,Qt::KeepAspectRatio);
    const auto image=decodeRawPreview(wire,maximum,2,format);
    check(image==referenceThumbnail(pixels,size),"CFA/resize reference");
    check(image.sizeInBytes()<=size_t(size.width()+3)*size.height(),"bounded display allocation");
    check(isRaw8Preview(image)&&!isNativeBayerPreview(image),"thumbnail provenance");
  }
  check(wire==original&&decodeRawPreview(wire,{},2,format)==full,"zoom/return mutates original");
  check(decodeRawPreview(wire,QSize(6000,4000),2,format)==full,"oversized zoom");
  auto rejects=[&](const std::vector<uint8_t>& bytes,unsigned camera,uint8_t codec){
    RawPreviewMetadata m;m.source_frame=999;try{decodeRawPreview(bytes,QSize(320,192),camera,codec,&m);return false;}
    catch(const std::exception&){return m.source_frame==999;}};
  check(rejects(wire,1,format)&&rejects(wire,2,4)&&rejects(wire,2,prism::capture::kRaw8Zstd),"camera/retired/CPU codec guard");
  wire[38]=4;put(wire,52,456);put(wire,28,64000);put(wire,32,2048);sealHeader(wire);
  const auto hint=rawPreviewGroupHint(wire,2,format);
  check(hint&&hint->group_id==456&&hint->timestamp==2345678,"true group hint");
  check(!rawPreviewGroupHint(wire,1,format)&&!rawPreviewGroupHint(wire,2,4),"group identity guard");
  RawPreviewMetadata m;decodeRawPreview(wire,QSize(320,192),2,format,&m);
  check(m.trigger_sequence==456&&m.source_frame==19&&m.timestamp_us==2345678&&m.exposure_us==64000&&m.gain_x1024==2048,"same-frame metadata");
  auto bad=wire;bad[52]^=1;check(!rawPreviewGroupHint(bad,2,format)&&rejects(bad,2,format),"header CRC guard");
  bad=wire;bad[81]^=1;check(rejects(bad,2,format),"encoded CRC guard");
  {
    auto current=wire;metadataOnly(current);current[81]^=1;
    const auto decoded=decodeRawPreview(current,{},2,format);
    check(decoded.constScanLine(0)[0]==uint8_t(pixels[0]^1),"current-format pixel changed or rejected");
    auto broken=current;broken[24]^=1;check(rejects(broken,2,format),"current-format header guard");
    broken=current;broken[80]=255;check(rejects(broken,2,format),"current-format codec guard");
  }
  bad=wire;put(bad,20,prism::capture::rawLe32(bad.data()+20)^1);sealHeader(bad);
  check(rejects(bad,2,format),"full decoded CRC even for thumbnail");
  for(unsigned depth:{10,12,16}){bad=wire;bad[6]=uint8_t(depth);sealHeader(bad);check(rejects(bad,2,format),"native depth guard");}
  wire[38]=0;put(wire,40,0);put(wire,44,0);put(wire,52,0);sealHeader(wire);
  decodeRawPreview(wire,QSize(320,192),2,format,&m);
  check(!m.timestamp_us&&!m.trigger_sequence&&!rawPreviewGroupHint(wire,2,format),"unknown time fabricated");
  check(rawTimingDescription(4,2345678,false).contains("2.345678 s")&&
        !rawTimingDescription(4,2345678,false).contains("UTC")&&rawTimingDescription(0,0,false).contains("unknown"),"time semantics");
  for(unsigned shift=0;shift<2;++shift){
    const uint8_t phase[4]={40,180,180,80};
    for(unsigned y=0;y<3072;++y)for(unsigned x=0;x<5120;++x)pixels[size_t(y)*5120+x]=phase[((y+shift)&1)*2+((x+shift)&1)];
    wire=verbatim(pixels);const auto saved=wire;
    for(QSize maximum:{QSize(640,512),QSize(233,140),QSize(226,136),QSize(317,191),QSize(160,100),QSize(4000,2400)}){
      auto image=decodeRawPreview(wire,maximum,2,format);
      for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)check(image.constScanLine(y)[x]==120,"CFA checkerboard regression");
      check(wire==saved,"display changed original");
    }
  }
  std::cout<<"PASS native RAW8 exact pixels/full CRC/group/unknown timing/zoom/CFA phases\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
