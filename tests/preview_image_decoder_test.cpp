#include "ui/preview_image_decoder.hpp"

#include <QtCore/QBuffer>
#include <QtCore/QByteArray>
#include <QtCore/QCoreApplication>
#include <QtGui/QImage>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <utility>
#include <vector>

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  QImage source(1280, 1024, QImage::Format_RGB32);
  for (int y = 0; y < source.height(); ++y) {
    auto* row = reinterpret_cast<QRgb*>(source.scanLine(y));
    for (int x = 0; x < source.width(); ++x) {
      row[x] = qRgb(x & 0xff, y & 0xff, (x + y) & 0xff);
    }
  }

  QByteArray encoded;
  QBuffer output(&encoded);
  if (!output.open(QIODevice::WriteOnly) ||
      !source.save(&output, "JPG", 85)) {
    std::cerr << "failed to encode JPEG fixture\n";
    return 1;
  }
  const std::vector<uint8_t> jpeg(
      reinterpret_cast<const uint8_t*>(encoded.constData()),
      reinterpret_cast<const uint8_t*>(encoded.constData()) + encoded.size());

  const QImage preview =
      prism_viewer::ui::decodePreviewJpeg(jpeg, QSize(640, 512));
  if (preview.size() != QSize(640, 512)) {
    std::cerr << "scaled decode produced " << preview.width() << "x"
              << preview.height() << "\n";
    return 2;
  }

  const QImage full =
      prism_viewer::ui::decodePreviewJpeg(jpeg, QSize());
  if (full.size() != source.size()) {
    std::cerr << "full decode produced " << full.width() << "x"
              << full.height() << "\n";
    return 3;
  }

  const std::vector<uint8_t> corrupt = {0x00, 0x01, 0x02, 0x03};
  if (!prism_viewer::ui::decodePreviewJpeg(
           corrupt, QSize(640, 512)).isNull()) {
    std::cerr << "corrupt JPEG unexpectedly decoded\n";
    return 4;
  }

  constexpr int kBenchmarkDecodes = 120;
  auto benchmark = [&jpeg](const QSize& maximum_size) {
    const auto started = std::chrono::steady_clock::now();
    qint64 decoded_pixels = 0;
    for (int index = 0; index < kBenchmarkDecodes; ++index) {
      const QImage image =
          prism_viewer::ui::decodePreviewJpeg(jpeg, maximum_size);
      if (image.isNull()) return std::pair<double, qint64>{-1.0, 0};
      decoded_pixels +=
          static_cast<qint64>(image.width()) * image.height();
    }
    return std::pair<double, qint64>{
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started)
            .count(),
        decoded_pixels};
  };
  const auto scaled_benchmark = benchmark(QSize(640, 512));
  const auto full_benchmark = benchmark(QSize());
  if (scaled_benchmark.first < 0.0 || full_benchmark.first < 0.0) return 5;

  std::cout << "preview_image_decoder_test=PASS scaled_120_ms="
            << scaled_benchmark.first << " full_120_ms="
            << full_benchmark.first << " scaled_pixels="
            << scaled_benchmark.second << " full_pixels="
            << full_benchmark.second << "\n";
  return 0;
}
