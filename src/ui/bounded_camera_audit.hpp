#pragma once
#include <array>
#include <condition_variable>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <thread>

// Qualification only. Four ordered lanes; two queued + one processing per lane.
// Never blocks USB on decode/disk, never drops or grows an unbounded queue.
template<class Image> class BoundedCameraAudit {
 public:
  using Audit = std::function<void(const Image&)>;
  explicit BoundedCameraAudit(Audit audit) : audit_(std::move(audit)) {
    try { for (unsigned c=0;c<4;++c) workers_[c]=std::thread([this,c]{run(c);}); }
    catch (...) { join(); throw; }
  }
  ~BoundedCameraAudit() { join(); }
  void submit(const Image& image) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (failure_) std::rethrow_exception(failure_);
    if (closing_ || image.camera>=4) throw std::runtime_error("audit closed/invalid camera");
    auto& queue=queues_[image.camera];
    if (queue.size()>=2) {
      failure_=std::make_exception_ptr(std::runtime_error("bounded RAW audit queue full"));
      std::rethrow_exception(failure_);
    }
    queue.push_back(image);
    ready_.notify_all();
  }
  void finish() { join(); if(failure_)std::rethrow_exception(failure_); }
 private:
  void join() noexcept {
    {std::lock_guard<std::mutex> lock(mutex_);closing_=true;}
    ready_.notify_all();
    for(auto& worker:workers_)if(worker.joinable())worker.join();
  }
  void run(unsigned c) noexcept {
    try {
      for(;;){
        Image image;
        {std::unique_lock<std::mutex> lock(mutex_);
         ready_.wait(lock,[&]{return closing_||!queues_[c].empty();});
         if(queues_[c].empty())return;
         image=std::move(queues_[c].front());queues_[c].pop_front();}
        audit_(image);
      }
    } catch (...) {std::lock_guard<std::mutex> lock(mutex_);if(!failure_)failure_=std::current_exception();}
  }
  Audit audit_;
  std::mutex mutex_;
  std::condition_variable ready_;
  std::array<std::deque<Image>,4> queues_;
  std::array<std::thread,4> workers_;
  std::exception_ptr failure_;
  bool closing_=false;
};
