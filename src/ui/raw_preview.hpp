#pragma once
#include "prism/usb/profile_video.hpp"
#include <QImage>
#include <QSize>
namespace prism_viewer::ui {
// Display only: fixed linear12->8 mapping, without ISP/demosaicing. The
// losslessly decoded packed12 is never overwritten or presented as ISP color.
inline QImage decodeRawPreview(const std::vector<uint8_t>& bytes,const QSize& maximum,unsigned camera,uint8_t format=0) {
  auto raw=prism::capture::decodeRaw12(bytes,format);
  if(raw.camera!=camera)throw std::runtime_error("RAW12 camera identity mismatch");
  // USB always keeps/validates the complete lossless RAW. Only its display
  // allocation changes: normal tiles never allocate a full-resolution QImage.
  // An invalid maximum is the existing selected-camera zoom request.
  const QSize native(int(raw.width),int(raw.height));
  QSize display=native;
  if(maximum.isValid()&&!maximum.isEmpty()&&
      (maximum.width()<native.width()||maximum.height()<native.height()))
    display=native.scaled(maximum,Qt::KeepAspectRatio);
  if(display.isEmpty())return {};
  QImage result(display,QImage::Format_Grayscale8);
  if(result.isNull())return {};
  for(unsigned y=0;y<unsigned(display.height());y++) {
    auto* dst=result.scanLine(int(y));
    const size_t sy=uint64_t(y)*raw.height/unsigned(display.height());
    for(unsigned x=0;x<unsigned(display.width());x++) {
      const size_t sx=uint64_t(x)*raw.width/unsigned(display.width());
      dst[x]=uint8_t(prism::capture::raw12Pixel(raw,sy*raw.width+sx)>>4);
    }
  }
  return result;
}
}
