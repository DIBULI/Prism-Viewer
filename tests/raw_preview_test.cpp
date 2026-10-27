#include "ui/raw_preview.hpp"
#include "ui/raw_preview_group.hpp"
#include <QCoreApplication>
#include <iostream>

// Independent packed12 oracle, including all four CFA phases.
QImage referenceThumbnail(const std::vector<uint8_t>& packed,QSize size) {
  auto pixel=[&](size_t index) {
    const size_t at=index/2*3;
    return unsigned(packed[at+(index&1)])*16+
        ((packed[at+2]>>((index&1)*4))&15);
  };
  QImage cells(2560,1536,QImage::Format_Grayscale8);
  for(unsigned y=0;y<1536;++y)for(unsigned x=0;x<2560;++x){
    const size_t at=size_t(y*2)*5120+x*2;
    cells.scanLine(y)[x]=uint8_t((pixel(at)+pixel(at+1)+pixel(at+5120)+pixel(at+5121))/64);
  }
  return cells.scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation)
      .convertToFormat(QImage::Format_Grayscale8);
}

int main(int argc,char** argv) {
  QCoreApplication app(argc,argv);
  std::vector<uint8_t> packed(23592960);
  for(size_t n=0;n<packed.size()/3;n++) {
    unsigned a=(n*2)&4095,b=(n*2+1)&4095;
    packed[n*3]=a>>4;packed[n*3+1]=b>>4;
    packed[n*3+2]=(a&15)|((b&15)<<4);
  }
  std::vector<uint8_t> wire(48+ZSTD_compressBound(packed.size()));
  auto put=[&](size_t at,uint32_t v){for(unsigned n=0;n<4;n++)wire[at+n]=uint8_t(v>>(8*n));};
  std::memcpy(wire.data(),"PRZ1",4);
  wire[4]=1;wire[5]=48;wire[6]=12;wire[36]=2;wire[37]=2;wire[38]=3;
  put(8,5120);put(12,3072);put(16,packed.size());
  put(20,prism::capture::rawCrc32(packed.data(),packed.size()));
  auto bytes=ZSTD_compress(wire.data()+48,wire.size()-48,packed.data(),packed.size(),1);
  if(ZSTD_isError(bytes))return 1;
  wire.resize(48+bytes);
  const auto original=wire;
  auto full=prism_viewer::ui::decodeRawPreview(wire,{},2);
  if(full.size()!=QSize(5120,3072)||full.format()!=QImage::Format_Grayscale8)return 2;
  for(unsigned x=0;x<4096;x++)if(full.constScanLine(0)[x]!=(x>>4))return 3;
  auto small=prism_viewer::ui::decodeRawPreview(wire,QSize(640,512),2);
  if(small.size()!=QSize(640,384)||wire!=original)return 4;
  bool rejected=false;
  try{prism_viewer::ui::decodeRawPreview(wire,{},1);}catch(const std::runtime_error&){rejected=true;}
  if(!rejected)return 5;
  wire[20]^=1;rejected=false;
  try{prism_viewer::ui::decodeRawPreview(wire,{},2);}catch(const std::runtime_error&){rejected=true;}
  if(!rejected)return 6;
  // Independent fixed-residual fixture in the new GPU envelope. No GPU or
  // encoder implementation is reused to manufacture this reference.
  std::vector<uint8_t> rice(64),u16(5120u*3072u*2u);
  for(size_t i=0;i<u16.size()/2;i++){u16[i*2]=uint8_t(i&255);u16[i*2+1]=uint8_t((i&4095)>>8);}
  for(size_t base=0;base<u16.size()/2;base+=256){
    rice.push_back(3);rice.insert(rice.end(),u16.begin()+base*2,u16.begin()+base*2+4);
    const size_t start=rice.size();rice.resize(start+96);
    for(unsigned i=0;i<254;i++){unsigned bit=i*3+2;rice[start+bit/8]|=uint8_t(1u<<(bit%8));}
  }
  auto put_rice=[&](size_t at,uint32_t v){for(unsigned n=0;n<4;n++)rice[at+n]=uint8_t(v>>(8*n));};
  std::memcpy(rice.data(),"PRG1",4);rice[4]=1;rice[5]=64;rice[6]=12;rice[36]=2;rice[37]=2;rice[38]=3;
  put_rice(8,5120);put_rice(12,3072);put_rice(16,23592960);
  put_rice(20,prism::capture::rawCrc32(u16.data(),u16.size()));
  put_rice(48,prism::capture::rawCrc32(rice.data()+64,rice.size()-64));put_rice(52,256);put_rice(56,rice.size()-64);
  if(prism::capture::decodeRaw12Rice(rice).packed!=packed)return 7;
  auto gpu_full=prism_viewer::ui::decodeRawPreview(rice,{},2,prism::capture::kRaw12Rice);
  if(gpu_full!=full)return 8;
  auto gpu_small=prism_viewer::ui::decodeRawPreview(rice,QSize(640,512),2,prism::capture::kRaw12Rice);
  if(gpu_small!=small)return 9;
  if(gpu_small.sizeInBytes()!=640*384)return 10;
  if(gpu_small!=referenceThumbnail(packed,QSize(640,384)))return 11;
  auto narrow=prism_viewer::ui::decodeRawPreview(rice,QSize(320,192),2,prism::capture::kRaw12Rice);
  if(narrow.size()!=QSize(320,192))return 12;
  if(narrow!=referenceThumbnail(packed,QSize(320,192)))return 13;
  // Opening zoom after thumbnails uses the original bytes, never upscales a
  // cached thumbnail. Closing zoom returns to the small display allocation.
  if(prism_viewer::ui::decodeRawPreview(rice,{},2,4)!=gpu_full ||
     prism_viewer::ui::decodeRawPreview(rice,QSize(640,512),2,4)!=gpu_small)return 14;
  if(prism_viewer::ui::decodeRawPreview(rice,QSize(6000,4000),2,4).size()!=gpu_full.size())return 15;
  // UI metadata is from the SAME fully validated image, never the most recent
  // receiver stats or an unvalidated header. Trigger time is not implicit UTC.
  prism_viewer::ui::RawPreviewMetadata metadata;
  rice[38]=4;put_rice(40,2345678);put_rice(24,19);put_rice(28,64000);put_rice(32,2048);
  const auto hint=prism_viewer::ui::rawPreviewGroupHint(rice,2,4);
  if(hint)return 21; // v1 does not contain an actual group ID.
  if(prism_viewer::ui::rawPreviewGroupHint(rice,1,4)||
     prism_viewer::ui::rawPreviewGroupHint(rice,2,3))return 22;
  prism_viewer::ui::decodeRawPreview(rice,QSize(640,512),2,4,&metadata);
  if(metadata.timing_mode!=4||metadata.timestamp_us!=2345678||metadata.source_frame!=19||
     metadata.exposure_us!=64000||metadata.gain_x1024!=2048)return 17;
  const auto legacy=rice;
  rice.insert(rice.begin()+64,16,0);rice[4]=2;rice[5]=80;put_rice(64,456);
  put_rice(76,prism::capture::rawHeaderCrc32(rice.data(),76));
  const auto group_hint=prism_viewer::ui::rawPreviewGroupHint(rice,2,4);
  if(!group_hint||group_hint->group_id!=456||group_hint->timestamp!=2345678)return 24;
  auto grouped=prism_viewer::ui::decodeRawPreview(rice,QSize(640,512),2,4,&metadata);
  if(grouped!=gpu_small||metadata.trigger_sequence!=456||metadata.source_frame!=19)return 25;
  rice[64]^=1;
  if(prism_viewer::ui::rawPreviewGroupHint(rice,2,4))return 26;
  rejected=false;try{prism_viewer::ui::decodeRawPreview(rice,{},2,4);}
  catch(const std::runtime_error&){rejected=true;}if(!rejected)return 27;
  rice=legacy;
  using prism_viewer::ui::rawTimingDescription;
  if(!rawTimingDescription(4,2345678,false).contains("2.345678 s")||
     !rawTimingDescription(4,2345678,true).contains(QStringLiteral("共同外触发"))||
     rawTimingDescription(4,2345678,false).contains("UTC")||
     !rawTimingDescription(3,0,false).contains("diagnostic")||
     !rawTimingDescription(0,0,false).contains("unknown"))return 18;
  rice[38]=0;put_rice(40,0);
  if(prism_viewer::ui::rawPreviewGroupHint(rice,2,4))return 23;
  prism_viewer::ui::decodeRawPreview(rice,QSize(640,512),2,4,&metadata);
  if(metadata.timing_mode||metadata.timestamp_us)return 19;
  // Full USB pixel CRC remains enforced even for the thumbnail display.
  rice[20]^=1;rejected=false;
  metadata.source_frame=999;
  try{prism_viewer::ui::decodeRawPreview(rice,QSize(640,512),2,4,&metadata);}
  catch(const std::runtime_error&){rejected=true;}if(!rejected)return 16;
  if(metadata.source_frame!=999)return 20;
  using namespace prism_viewer::ui;
  if(!isNativeBayerPreview(full)||!isRaw12Preview(small)||isNativeBayerPreview(small))return 21;
  // Uniform colored illumination: CFA phase values vary, scene does not.
  // Two alignments, integer and non-integer resize ratios, enlarged thumbnails.
  for(unsigned shift=0;shift<2;++shift){
    const unsigned phase[4]={641,2881,2881,1281}; // Mean 1921 -> fixed 8-bit120.
    for(unsigned y=0;y<3072;++y)for(unsigned x=0;x<5120;x+=2){
      const size_t at=(size_t(y)*5120+x)/2*3;
      unsigned a=phase[((y+shift)&1)*2+shift],b=phase[((y+shift)&1)*2+(1-shift)];
      packed[at]=a>>4;packed[at+1]=b>>4;packed[at+2]=(a&15)|((b&15)<<4);
    }
    wire.resize(48+ZSTD_compressBound(packed.size()));
    put(20,prism::capture::rawCrc32(packed.data(),packed.size()));
    bytes=ZSTD_compress(wire.data()+48,wire.size()-48,packed.data(),packed.size(),1);
    if(ZSTD_isError(bytes))return 22;
    wire.resize(48+bytes);
    const auto saved=wire;
    for(const QSize maximum:{QSize(640,512),QSize(233,140),QSize(226,136),
        QSize(317,191),QSize(160,100),QSize(4000,2400)}){
      auto image=decodeRawPreview(wire,maximum,2);
      if(image.size()!=QSize(5120,3072).scaled(maximum,Qt::KeepAspectRatio))return 23;
      for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)
        if(image.constScanLine(y)[x]!=120)return 24;
      if(wire!=saved||!isRaw12Preview(image)||isNativeBayerPreview(image))return 25;
    }
  }
  std::cout<<"PASS RAW12 full USB CRC, immutable original, bounded CFA-aware thumbnails, multi-size/phase, native zoom/return, same-frame metadata\n";
}
