#pragma once

#include <QtGui/QImage>
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
#include <QtGui/QColorSpace>
#endif
#include "ui/srgb_transfer.hpp"

namespace prism_viewer::ui {

// Display-only provenance, attached only after a complete 415M RAW12 decode.
// It is not wire metadata and never changes or accompanies recorded RAW bytes.
inline constexpr auto kRaw12PreviewKey = "prism.raw12.preview";
inline constexpr auto kRaw12TransferKey = "prism.raw12.transfer";

inline bool isRaw12Preview(const QImage& image) {
  const auto kind = image.text(kRaw12PreviewKey);
  return kind == QStringLiteral("native-bayer") ||
         kind == QStringLiteral("cell-average");
}

inline bool isNativeBayerPreview(const QImage& image) {
  return image.text(kRaw12PreviewKey) == QStringLiteral("native-bayer") &&
         image.text(kRaw12TransferKey) != QStringLiteral("srgb") &&
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

// Display-only copy: caches, decoded samples and recording/AE stay linear.
// Unmarked JPEG/41M3 images are already display-ready and must not be encoded.
inline QImage raw12SrgbDisplay(const QImage& image) {
  if (!isRaw12Preview(image) ||
      image.text(kRaw12TransferKey) == QStringLiteral("srgb")) return image;
  // Qt5 smooth scaling promotes Grayscale8 to RGB32 (still linear gray).
  // Normalize the display copy, otherwise those paths would skip the LUT.
  QImage display = image.format() == QImage::Format_Grayscale8
      ? image.copy() : image.convertToFormat(QImage::Format_Grayscale8);
  if (display.isNull()) return {};
  const auto& lut = prism_preview::linear8ToSrgb();
  for (int y = 0; y < display.height(); ++y) {
    auto* row = display.scanLine(y);
    for (int x = 0; x < display.width(); ++x) row[x] = lut[row[x]];
  }
  display.setText(kRaw12TransferKey, QStringLiteral("srgb"));
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
  // Qt 5.12 has no color-space metadata API; its pixels use the same LUT.
  display.setColorSpace(QColorSpace::SRgb);
#endif
  return display;
}

}  // namespace prism_viewer::ui
