#pragma once

#include <QtGui/QImage>

namespace prism_viewer::ui {

// Display-only provenance, attached only after a complete 415M RAW12 decode.
// It is not wire metadata and never changes or accompanies recorded RAW bytes.
inline constexpr auto kRaw12PreviewKey = "prism.raw12.preview";

inline bool isRaw12Preview(const QImage& image) {
  const auto kind = image.text(kRaw12PreviewKey);
  return kind == QStringLiteral("native-bayer") ||
         kind == QStringLiteral("cell-average");
}

inline bool isNativeBayerPreview(const QImage& image) {
  return image.text(kRaw12PreviewKey) == QStringLiteral("native-bayer") &&
         image.format() == QImage::Format_Grayscale8 &&
         image.size() == QSize(5120, 3072);
}

// Equal weight for every CFA phase, independent of Bayer order. This is a
// linear monochrome preview, not a color demosaic or automatic tone mapping.
// Cache this bounded 1/4-size display image once per frame, not per resize.
inline QImage raw12ThumbnailSource(const QImage& image) {
  if (!isNativeBayerPreview(image)) return image;
  QImage average(image.width() / 2, image.height() / 2,
                 QImage::Format_Grayscale8);
  if (average.isNull()) return {};
  for (int y = 0; y < average.height(); ++y) {
    const auto* a = image.constScanLine(y * 2);
    const auto* b = image.constScanLine(y * 2 + 1);
    auto* dst = average.scanLine(y);
    for (int x = 0; x < average.width(); ++x) {
      const int sx = x * 2;
      dst[x] = uint8_t((unsigned(a[sx]) + a[sx + 1] +
                        b[sx] + b[sx + 1]) / 4);
    }
  }
  average.setText(kRaw12PreviewKey, QStringLiteral("cell-average"));
  return average;
}

}  // namespace prism_viewer::ui
