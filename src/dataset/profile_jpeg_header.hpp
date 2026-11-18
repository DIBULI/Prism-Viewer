#pragma once
#include "dataset/dataset_browser.hpp"
#include "communication/profile_frame_metadata.hpp"
#include <stdexcept>

namespace prism_viewer::dataset {
// Validate the recorded JPEG envelope against its index. Used by playback and
// image export; neither path synthesizes missing exposure/gain or timestamps.
inline void readProfileJpegHeader(const uint8_t* b,size_t size,DatasetImageEntry& e) {
  const auto m=profileJpegMetadata(b,size,e.byte_size,e.camera);
  if(!m) throw std::runtime_error("Invalid or missing JPEG source metadata");
  if(m->source_frame!=e.frame_id||m->timing_mode!=e.timing_mode||
      m->timestamp_us!=e.timestamp_us||m->trigger_sequence!=e.trigger_sequence)
    throw std::runtime_error("JPEG header/index mismatch");
  e.exposure_us=m->exposure_us;e.gain_x1024=m->gain_x1024;
}
}
