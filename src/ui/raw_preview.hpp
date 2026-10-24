#pragma once
#include "prism/usb/profile_video.hpp"
#include <QImage>
#include <QSize>
#include "raw_timing.hpp"
#include "bayer_preview.hpp"
namespace prism_viewer::ui {
struct RawPreviewMetadata {
  uint8_t timing_mode=0;
  uint64_t timestamp_us=0;
  uint32_t source_frame=0,exposure_us=0,gain_x1024=0;
};
// Display only: fixed linear12->8 mapping, without ISP/demosaicing. The
// losslessly decoded packed12 is never overwritten or presented as ISP color.
inline QImage decodeRawPreview(const std::vector<uint8_t>& bytes,const QSize& maximum,unsigned camera,uint8_t format=0,
                              RawPreviewMetadata* metadata=nullptr) {
  auto raw=prism::capture::decodeRaw12(bytes,format);
  if(raw.camera!=camera)throw std::runtime_error("RAW12 camera identity mismatch");
  if(metadata)*metadata={raw.timing_mode,raw.source_timestamp_us,raw.frame_id,raw.exposure_us,raw.gain_x1024};
  // USB always keeps/validates the complete lossless RAW. Only its display
  // allocation changes: normal tiles never allocate a full-resolution QImage.
  // A bounded 1/4-resolution CFA-cell average precedes every thumbnail resize.
  // An invalid maximum is the existing selected-camera zoom request.
  const QSize native(int(raw.width),int(raw.height));
  QSize display=native;
  if(maximum.isValid()&&!maximum.isEmpty()&&
      (maximum.width()<native.width()||maximum.height()<native.height()))
    display=native.scaled(maximum,Qt::KeepAspectRatio);
  if(display.isEmpty())return {};
  const bool native_view=display==native;
  QImage result(native_view ? native : QSize(raw.width/2,raw.height/2),
                QImage::Format_Grayscale8);
  if(result.isNull())return {};
  for(unsigned y=0;y<unsigned(result.height());y++) {
    auto* dst=result.scanLine(int(y));
    for(unsigned x=0;x<unsigned(result.width());x++) {
      if(native_view) {
        dst[x]=uint8_t(prism::capture::raw12Pixel(raw,size_t(y)*raw.width+x)>>4);
      } else {
        const size_t at=size_t(y*2)*raw.width+x*2;
        const unsigned sum=prism::capture::raw12Pixel(raw,at)+
            prism::capture::raw12Pixel(raw,at+1)+
            prism::capture::raw12Pixel(raw,at+raw.width)+
            prism::capture::raw12Pixel(raw,at+raw.width+1);
        dst[x]=uint8_t(sum/64); // Mean of all four 12-bit CFA phases, then 12->8.
      }
    }
  }
  if(!native_view && result.size()!=display)
    result=result.scaled(display,Qt::IgnoreAspectRatio,Qt::SmoothTransformation)
        .convertToFormat(QImage::Format_Grayscale8);
  result.setText(kRaw12PreviewKey,native_view ? QStringLiteral("native-bayer")
                                            : QStringLiteral("cell-average"));
  return result;
}
}
