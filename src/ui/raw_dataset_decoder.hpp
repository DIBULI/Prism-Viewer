#pragma once
#include "dataset/raw12_dataset.hpp"
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

namespace prism_viewer::ui {
// One worker and one pending request: seeking never accumulates decoded frames.
// Keeps only one decoded image per camera; reused samples carry their own age.
class RawDatasetDecoder {
 public:
  struct Result {
    uint64_t generation = 0;
    std::array<QImage, 4> images;
    std::array<QString, 4> errors;
  };
  using Entries = std::array<dataset::DatasetImageEntry, 4>;
  explicit RawDatasetDecoder(std::function<void(Result)> deliver)
      : deliver_(std::move(deliver)), thread_([this]{run();}) {}
  ~RawDatasetDecoder() {
    { std::lock_guard<std::mutex> lock(mutex_); stop_ = true; }
    wake_.notify_one(); thread_.join();
  }
  void submit(uint64_t generation, Entries entries) {
    { std::lock_guard<std::mutex> lock(mutex_); pending_ = Request{generation, std::move(entries)}; }
    wake_.notify_one();
  }
 private:
  struct Request { uint64_t generation; Entries entries; };
  std::function<void(Result)> deliver_;
  std::mutex mutex_;
  std::condition_variable wake_;
  bool stop_ = false;
  std::optional<Request> pending_;
  std::thread thread_;
  void run() {
    Entries previous{}; Result cache;
    for (;;) {
      Request request;
      { std::unique_lock<std::mutex> lock(mutex_);
        wake_.wait(lock,[this]{return stop_ || pending_.has_value();});
        if (stop_) return;
        request = std::move(*pending_); pending_.reset(); }
      cache.generation = request.generation;
      for (size_t c = 0; c < 4; ++c) {
        { std::lock_guard<std::mutex> lock(mutex_); if (stop_) return; }
        const auto& entry = request.entries[c];
        if (!entry.byte_size) { cache.images[c] = {}; cache.errors[c].clear(); previous[c] = {}; continue; }
        const auto& old = previous[c];
        if (entry.absolute_path == old.absolute_path && entry.byte_offset == old.byte_offset &&
            entry.byte_size == old.byte_size && entry.raw_crc32 == old.raw_crc32 && !cache.images[c].isNull()) continue;
        cache.images[c] = {}; cache.errors[c].clear();
        try { cache.images[c] = dataset::loadRaw12DatasetImage(entry); }
        catch (const std::exception& e) { cache.errors[c] = QString::fromUtf8(e.what()); }
        previous[c] = entry;
      }
      deliver_(cache);
    }
  }
};
} // namespace prism_viewer::ui
