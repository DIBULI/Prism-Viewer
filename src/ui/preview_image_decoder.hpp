#pragma once

#include <QtCore/QSize>
#include <QtGui/QImage>

#include <cstdint>
#include <vector>

namespace prism_viewer::ui {

// Decodes a JPEG directly at a bounded presentation size when supported by
// the Qt JPEG plugin. An invalid maximum_size requests full resolution.
QImage decodePreviewJpeg(const std::vector<uint8_t>& encoded,
                         const QSize& maximum_size);

}  // namespace prism_viewer::ui
