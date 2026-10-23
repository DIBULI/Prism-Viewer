#include "ui/raw_preview.hpp"
#include <QCoreApplication>
#include <iostream>

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
  for(int y=0;y<384;++y)for(int x=0;x<640;++x)
    if(gpu_small.constScanLine(y)[x]!=gpu_full.constScanLine(y*8)[x*8])return 11;
  auto narrow=prism_viewer::ui::decodeRawPreview(rice,QSize(320,192),2,prism::capture::kRaw12Rice);
  if(narrow.size()!=QSize(320,192))return 12;
  for(int y=0;y<192;++y)for(int x=0;x<320;++x)
    if(narrow.constScanLine(y)[x]!=gpu_full.constScanLine(y*16)[x*16])return 13;
  // Opening zoom after thumbnails uses the original bytes, never upscales a
  // cached thumbnail. Closing zoom returns to the small display allocation.
  if(prism_viewer::ui::decodeRawPreview(rice,{},2,4)!=gpu_full ||
     prism_viewer::ui::decodeRawPreview(rice,QSize(640,512),2,4)!=gpu_small)return 14;
  if(prism_viewer::ui::decodeRawPreview(rice,QSize(6000,4000),2,4).size()!=gpu_full.size())return 15;
  // Full USB pixel CRC remains enforced even for the thumbnail display.
  rice[20]^=1;rejected=false;
  try{prism_viewer::ui::decodeRawPreview(rice,QSize(640,512),2,4);}
  catch(const std::runtime_error&){rejected=true;}if(!rejected)return 16;
  std::cout<<"PASS RAW12 full USB CRC, immutable original, bounded local thumbnail, exact samples, zoom/return, no thumbnail upscaling\n";
}
