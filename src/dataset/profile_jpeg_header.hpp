#pragma once
#include "dataset/dataset_browser.hpp"
#include "prism/usb/profile_video.hpp"
#include <cstring>
#include <stdexcept>

namespace prism_viewer::dataset {
// Validate the recorded JPEG envelope against its index. Used by playback and
// image export; neither path synthesizes missing exposure/gain or timestamps.
inline void readProfileJpegHeader(const uint8_t* b,size_t size,DatasetImageEntry& e) {
  using prism::capture::rawLe32;
  if(size<70||b[0]!=255||b[1]!=216||b[2]!=255||b[3]!=239||b[4]!=0||b[5]!=66)
    throw std::runtime_error("Missing JPEG source metadata");
  const auto* p=b+6;
  if(std::memcmp(p,"PRJ1",4)||(p[4]!=1&&p[4]!=2)||p[5]!=64||p[28]!=e.camera||p[29]!=1||
      rawLe32(p+8)!=1280||rawLe32(p+12)!=1024||rawLe32(p+16)!=e.frame_id||p[30]!=e.timing_mode||
      (rawLe32(p+32)|(uint64_t(rawLe32(p+36))<<32))!=e.timestamp_us||
      e.byte_size<68||rawLe32(p+44)!=e.byte_size-68||
      rawLe32(p+(p[4]==2?60:48))!=prism::capture::rawCrc32(p,p[4]==2?60:48)||
      e.trigger_sequence!=(p[4]==2?rawLe32(p+48):0))
    throw std::runtime_error("JPEG header/index mismatch");
  e.exposure_us=rawLe32(p+20);e.gain_x1024=rawLe32(p+24);
}
}
