#include "dataset/image_sequence_exporter.hpp"
#include "dataset/raw8_dataset.hpp"
#include "common/ui_text.hpp"
#include "prism/usb/profile_video.hpp"
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QImageReader>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace prism_viewer::dataset {
namespace {
namespace fs = std::filesystem;
void need(bool ok,const std::string& message) { if(!ok)throw std::runtime_error(message); }
struct Cancelled {};
void cancel(const DatasetValidationCancelCallback& fn) {if(fn&&fn())throw Cancelled{};}
bool inside(const fs::path& p,const fs::path& root) {
  auto a=p.begin(); for(auto b=root.begin();b!=root.end();++b,++a)if(a==p.end()||*a!=*b)return false;
  return true;
}
void localFile(const fs::path& p,const fs::path& root) {
  need(fs::is_regular_file(fs::symlink_status(p))&&inside(fs::canonical(p),fs::canonical(root)),
       "Missing, linked or non-local dataset file: "+p.filename().string());
}
QByteArray read(const fs::path& p,uint64_t offset,uint64_t bytes,const fs::path& root) {
  localFile(p,root);const auto size=fs::file_size(p);
  need(bytes<=prism::capture::kProfileImageLimit&&offset<=size&&bytes<=size-offset,"Invalid image byte range");
  QFile f(common::fromFilesystemPath(p));need(f.open(QIODevice::ReadOnly)&&f.seek(qint64(offset)),"Cannot read image file");
  auto b=f.read(qint64(bytes));need(uint64_t(b.size())==bytes,"Truncated image file");return b;
}
uint64_t integer(const std::string& s) {
  need(!s.empty(),"Missing numeric metadata");uint64_t n=0;
  for(char c:s){need(c>='0'&&c<='9'&&n<=(UINT64_MAX-unsigned(c-'0'))/10,"Invalid numeric metadata");n=n*10+unsigned(c-'0');}
  return n;
}
uint64_t number(const QJsonObject& o,const char* k,uint64_t max=UINT32_MAX) {
  auto v=o.value(QLatin1String(k));double n=v.toDouble(-1);
  need(v.isDouble()&&std::isfinite(n)&&n>=0&&n<=double(max)&&std::floor(n)==n,std::string("Invalid index field: ")+k);
  return uint64_t(n);
}
std::string csv(const std::string& s) {
  std::string out="\"";for(char c:s){if(c=='"')out+='"';out+=c;}return out+'"';
}
void write(const fs::path& p,const QByteArray& data) {
  QFile f(common::fromFilesystemPath(p));need(f.open(QIODevice::WriteOnly|QIODevice::NewOnly),"Cannot create export file");
  need(f.write(data)==data.size()&&f.flush(),"Export write failed (disk full?)");
}
void jpegHeader(const QByteArray& bytes,DatasetImageEntry& e) {
  const auto* b=reinterpret_cast<const uint8_t*>(bytes.constData());
  need(bytes.size()>=70&&b[0]==255&&b[1]==216&&b[2]==255&&b[3]==239&&b[4]==0&&b[5]==66,"Missing JPEG source metadata");
  const auto* p=b+6;using prism::capture::rawLe32;
  need(!std::memcmp(p,"PRJ1",4)&&(p[4]==1||p[4]==2)&&p[5]==64&&p[28]==e.camera&&p[29]==1&&
      rawLe32(p+8)==1280&&rawLe32(p+12)==1024&&rawLe32(p+16)==e.frame_id&&p[30]==e.timing_mode&&
      (rawLe32(p+32)|(uint64_t(rawLe32(p+36))<<32))==e.timestamp_us&&rawLe32(p+44)==e.byte_size-68&&
      rawLe32(p+(p[4]==2?60:48))==prism::capture::rawCrc32(p,p[4]==2?60:48),"JPEG header/index mismatch");
  need(e.trigger_sequence==(p[4]==2?rawLe32(p+48):0),"JPEG trigger/index mismatch");
  e.exposure_us=rawLe32(p+20);e.gain_x1024=rawLe32(p+24);
}
}

ImageSequenceIndex inspectImageSequence(const std::filesystem::path& root,const DatasetValidationCancelCallback& cancelled) {
  ImageSequenceIndex result;cancel(cancelled);
  const auto identity=readDatasetIdentity(root);
  need(identity.model=="413GSV0"||identity.model=="415RSV0"||identity.model=="21312V0","Unsupported camera model");
  const auto info=root/"dataset.info";
  if(fs::exists(info)) {
    need(fs::file_size(info)<=1024*1024,"Oversized dataset.info");auto bytes=read(info,0,fs::file_size(info),root);
    if(bytes.startsWith("\xef\xbb\xbf"))bytes.remove(0,3);
    if(bytes.trimmed().startsWith('{')) {
      // Legacy 415 JSON recordings already have a strict, tested reader.
      auto data=loadRaw8Dataset(root,cancelled);result.cameras=std::move(data.cameras);result.profile=true;
      const auto original=QJsonDocument::fromJson(bytes).object();
      result.metadata["format"]="prism-raw8-dataset-v1";result.metadata["device_model"]=identity.model;
      result.metadata["device_serial"]=original.value("device_serial").toString().toStdString();
      result.metadata["legacy_manifest_json"]=bytes.toStdString();return result;
    }
    for(auto line:bytes.split('\n')) {
      line=line.trimmed();if(line.isEmpty()||line.startsWith('#'))continue;
      const auto at=line.indexOf('=');need(at>0,"Invalid dataset.info");
      need(result.metadata.emplace(line.left(at).toStdString(),line.mid(at+1).toStdString()).second,"Duplicate metadata key");
    }
  }
  auto& meta=result.metadata;meta["device_model"]=identity.model;
  const auto format=meta["format"];
  const bool mixed=format=="prism-mixed-dataset-v1";
  result.profile=mixed||format=="prism-raw8-dataset-v1";
  if(meta.count("complete"))need(meta["complete"]=="1","Recording is not complete; finish saving before exporting");
  if(result.profile) {
    need(meta["complete"]=="1"&&meta["camera_count"]=="4","Invalid completed camera manifest");
    need(mixed?(identity.model=="21312V0"&&meta["codec"]=="per-camera-jpeg-and-raw8-rice"):
        (identity.model=="415RSV0"&&meta["codec"]=="raw8-rice-lossless"),"Unsupported camera container");
  } else need(format.empty()||format=="prism-dataset-v3"||format=="prism-dataset-v4"||format=="prism-dataset-v5"||format=="prism-dataset-v6","Unsupported dataset format");
  size_t total=0;
  for(unsigned c=0;c<4;++c) {
    cancel(cancelled);auto& entries=result.cameras[c];
    if(!result.profile) {
      const auto path=root/("cam"+std::to_string(c)+".tum");if(!fs::exists(path))continue;
      localFile(path,root);need(fs::file_size(path)<=128*1024*1024,"Oversized camera index");
      if(!fs::file_size(path))continue;
      std::string error;need(loadDatasetImageIndex(root,c,&entries,&error),error);
      for(auto& e:entries){cancel(cancelled);e.camera=uint8_t(c);localFile(common::toFilesystemPath(e.absolute_path),root);}
    } else {
      const auto prefix="camera"+std::to_string(c);const bool raw=!mixed||c==1||c==2;
      const auto stream=root/(prefix+(raw?".pr8-stream":".mjpg-stream"));const auto idx=root/(prefix+".jsonl");
      localFile(stream,root);localFile(idx,root);QFile f(common::fromFilesystemPath(idx));need(f.open(QIODevice::ReadOnly),"Cannot open camera index");
      uint64_t offset=0;
      while(!f.atEnd()) {
        cancel(cancelled);auto line=f.readLine(4097);need(!line.isEmpty()&&line.size()<=4096,"Oversized index row");
        QJsonParseError error;auto doc=QJsonDocument::fromJson(line,&error);need(error.error==QJsonParseError::NoError&&doc.isObject(),"Invalid camera index JSON");auto row=doc.object();
        DatasetImageEntry e;e.camera=uint8_t(c);e.raw8=raw;e.absolute_path=common::fromFilesystemPath(stream);
        e.byte_offset=number(row,"offset",9007199254740991ULL);e.byte_size=uint32_t(number(row,"bytes"));
        e.timestamp_us=number(row,"timestamp_us",9007199254740991ULL);e.frame_id=uint32_t(number(row,"frame_id"));
        e.timing_mode=uint8_t(number(row,"timing_mode",4));e.trigger_sequence=row.contains("trigger_sequence")?uint32_t(number(row,"trigger_sequence")):0;
        e.raw_crc32=uint32_t(number(row,"raw_crc32"));e.encoded_crc32=uint32_t(number(row,"encoded_crc32"));
        e.dropped_before=row.contains("dropped_before")?number(row,"dropped_before"):0;
        need(e.byte_offset==offset&&e.byte_size>=80&&e.byte_size<=prism::capture::kProfileImageLimit,"Invalid frame range");
        need(offset<=fs::file_size(stream)&&e.byte_size<=fs::file_size(stream)-offset,"Truncated image container");
        auto h=read(stream,offset,80,root);
        if(raw) {
          auto header=prism::capture::parseRaw8Header(reinterpret_cast<const uint8_t*>(h.constData()),h.size(),e.byte_size,prism::capture::kRaw8Rice);
          need(header.camera==c&&header.frame_id==e.frame_id&&header.timestamp_us==e.timestamp_us&&header.timing_mode==e.timing_mode&&header.trigger_sequence==e.trigger_sequence&&header.raw_crc==e.raw_crc32&&header.encoded_crc==e.encoded_crc32,"RAW8 header/index mismatch");
          need(header.width==(mixed?4096u:5120u)&&header.height==3072,"Unexpected RAW8 geometry");
          e.exposure_us=header.exposure_us;e.gain_x1024=header.gain_x1024;
        } else jpegHeader(h,e);
        need(total+entries.size()<1000000,"Image index exceeds one million frames");entries.push_back(e);offset+=e.byte_size;
      }
      need(f.error()==QFile::NoError&&offset==fs::file_size(stream)&&entries.size()==integer(meta.at(prefix+"_images")),"Camera file/count mismatch");
    }
    total+=entries.size();need(total<=1000000,"Image index exceeds one million frames");
  }
  need(total>0,"Dataset contains no camera images");return result;
}

ImageSequenceExportResult exportImageSequence(const std::filesystem::path& root,const std::filesystem::path& output,
    const DatasetValidationProgressCallback& progress,const DatasetValidationCancelCallback& cancelled) {
  ImageSequenceExportResult result;
  try {
    need(!output.empty()&&!fs::exists(fs::symlink_status(output)),"Choose a new export directory; existing files will not be replaced");
    need(fs::is_directory(output.parent_path()),"Export parent directory does not exist");
    need(!inside(fs::weakly_canonical(output),fs::canonical(root)),"Export must be outside the source dataset");
    auto data=inspectImageSequence(root,cancelled);
    QTemporaryDir stage(common::fromFilesystemPath(output.parent_path()/".prism-image-export-XXXXXX"));need(stage.isValid(),"Cannot create export staging directory");
    const auto dir=common::toFilesystemPath(stage.path());uint64_t done=0;
    QJsonObject receipt{{"format","prism-image-sequence-v1"},{"device_model",QString::fromStdString(data.metadata["device_model"])},
      {"device_serial",QString::fromStdString(data.metadata["device_serial"])},{"raw_pixels","native 8-bit Bayer samples in grayscale PNG; no demosaic, gamma or white balance"},
      {"timestamp_policy","source values preserved per camera; no interpolation or synchronization assertion"}};
    QJsonObject provenance;for(const auto& item:data.metadata)provenance.insert(QString::fromStdString(item.first),QString::fromStdString(item.second));receipt.insert("source_metadata",provenance);
    for(unsigned c=0;c<4;++c) {
      const auto folder=dir/("camera"+std::to_string(c));fs::create_directory(folder);
      QFile index(common::fromFilesystemPath(folder/"frames.csv"));need(index.open(QIODevice::WriteOnly|QIODevice::NewOnly),"Cannot create image index");
      const QByteArray head="file,timestamp_us,exposure_us,gain_x1024,frame_id,trigger_sequence,timing_mode,dropped_before,width,height,pixel_format,bayer_code,source_container,source_offset,source_bytes\n";
      need(index.write(head)==head.size(),"Cannot write CSV header");
      uint64_t ordinal=0;
      for(const auto& entry:data.cameras[c]) {
        cancel(cancelled);std::ostringstream name;name<<std::setw(8)<<std::setfill('0')<<ordinal++<<'_'<<entry.timestamp_us<<(entry.raw8?".png":".jpg");
        const auto dest=folder/name.str();int width=0,height=0;std::string bayer;
        if(entry.raw8) {
          // Native full-resolution Bayer pixels. Never export the display thumbnail.
          localFile(common::toFilesystemPath(entry.absolute_path),root);auto image=loadRaw8DatasetImage(entry);width=image.width();height=image.height();
          auto header=read(common::toFilesystemPath(entry.absolute_path),entry.byte_offset,80,root);
          bayer=std::to_string(uint8_t(header[7])); // Preserve the source enum code.
          need(image.save(common::fromFilesystemPath(dest),"PNG"),"Cannot save PNG (disk full?)");
        } else {
          auto bytes=read(common::toFilesystemPath(entry.absolute_path),entry.byte_offset,entry.byte_size,root);
          need(bytes.size()>=4&&uint8_t(bytes[0])==255&&uint8_t(bytes[1])==216&&uint8_t(bytes[bytes.size()-2])==255&&uint8_t(bytes[bytes.size()-1])==217,"Invalid JPEG payload");
          if(data.profile){auto checked=entry;jpegHeader(bytes,checked);need(checked.exposure_us==entry.exposure_us&&checked.gain_x1024==entry.gain_x1024,"JPEG metadata changed");}
          QImage image=QImage::fromData(bytes,"JPG");need(!image.isNull(),"Cannot decode JPEG");width=image.width();height=image.height();
          if(data.profile)need(width==1280&&height==1024,"Unexpected JPEG geometry");
          write(dest,bytes); // Original bytes, including vendor APP metadata, no re-encoding.
        }
        std::ostringstream row;row<<csv(name.str())<<','<<entry.timestamp_us<<','<<entry.exposure_us<<',';
        if(data.profile)row<<entry.gain_x1024;row<<',';
        if(data.profile)row<<entry.frame_id;row<<',';if(data.profile)row<<entry.trigger_sequence;row<<',';
        if(data.profile)row<<unsigned(entry.timing_mode);row<<',';if(data.profile)row<<entry.dropped_before;
        row<<','<<width<<','<<height<<','<<(entry.raw8?"Bayer8":"JPEG")<<','<<bayer<<','
           <<csv(fs::relative(common::toFilesystemPath(entry.absolute_path),root).generic_u8string())<<','<<entry.byte_offset<<','<<entry.byte_size<<'\n';
        const auto line=QByteArray::fromStdString(row.str());need(index.write(line)==line.size(),"Cannot write image index");
        ++result.images[c];if(progress)progress(++done,"camera"+std::to_string(c));
      }
      need(index.flush(),"Cannot flush image index");receipt.insert(QStringLiteral("camera%1_images").arg(c),double(result.images[c]));
    }
    cancel(cancelled);receipt.insert("complete",true);write(dir/"export.json",QJsonDocument(receipt).toJson());
    // No replacement of an existing folder, including one created during export.
    need(!fs::exists(fs::symlink_status(output)),"Export destination appeared while exporting");
    need(QDir().rename(stage.path(),common::fromFilesystemPath(output)),"Cannot finalize export directory");stage.setAutoRemove(false);result.success=true;
  } catch(const Cancelled&) {result.cancelled=true;result.error="Export cancelled";}
    catch(const std::exception& e) {result.error=e.what();}
  return result;
}
}
