#include "ui/preview_image_decoder.hpp"

#include <QtCore/QBuffer>
#include <QtCore/QByteArray>
#include <QtCore/QIODevice>
#include <QtGui/QImageReader>

#include <limits>

namespace prism_viewer::ui {

QImage decodePreviewJpeg(const std::vector<uint8_t>& encoded,
                         const QSize& maximum_size) {
  if (encoded.empty() ||
      encoded.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
    return {};
  }

  QByteArray bytes = QByteArray::fromRawData(
      reinterpret_cast<const char*>(encoded.data()),
      static_cast<int>(encoded.size()));
  QBuffer buffer(&bytes);
  if (!buffer.open(QIODevice::ReadOnly)) return {};

  QImageReader reader(&buffer, "JPG");
  reader.setAutoTransform(false);
  if (maximum_size.isValid() && !maximum_size.isEmpty()) {
    const QSize source_size = reader.size();
    if (source_size.isValid()) {
      const QSize decode_size =
          source_size.scaled(maximum_size, Qt::KeepAspectRatio);
      if (decode_size.width() < source_size.width() ||
          decode_size.height() < source_size.height()) {
        reader.setScaledSize(decode_size);
      }
    }
  }
  return reader.read();
}

}  // namespace prism_viewer::ui
