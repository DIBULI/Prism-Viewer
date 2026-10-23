#include "ui/independent_preview_mailbox.hpp"
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>

using Mailbox = prism_viewer::ui::IndependentPreviewMailbox<unsigned>;
void check(bool ok) { if (!ok) throw std::runtime_error("mailbox assertion failed"); }

int main() {
  try {
    Mailbox box;
    box.reset(1);
    // Four lanes completing before Qt gets to process one event.
    for (unsigned c = 0; c < 4; ++c) {
      auto r = box.publish(c, c+1, 1, 100+c);
      check(r.accepted && r.post == (c == 0) && !r.replaced);
    }
    auto same = box.publish(2, 10, 1, 202);
    check(same.accepted && !same.post && same.replaced);
    check(!box.publish(2, 9, 1, 999).accepted); // slow older decoder
    auto batch = box.take(1);
    for (unsigned c = 0; c < 4; ++c) check(batch[c] && *batch[c] == (c == 2 ? 202 : 100+c));
    check(!box.publish(2, 9, 1, 999).accepted); // already displayed newer
    check(box.publish(2, 11, 1, 203).post);

    box.reset(2);
    check(!box.publish(0, 100, 1, 999).accepted);
    check(box.publish(0, 1, 2, 300).post);
    for (auto& f : box.take(1)) check(!f); // stale callback cannot clear new data
    check(!box.publish(1, 2, 2, 301).post);
    batch = box.take(2);
    check(batch[0] && *batch[0] == 300 && batch[1] && *batch[1] == 301);
    check(!batch[2] && !batch[3]);

    box.reset(3);
    std::array<std::thread, 4> workers;
    std::atomic<unsigned> posts{0};
    for (unsigned c = 0; c < 4; ++c) workers[c] = std::thread([&,c] {
      for (unsigned n = 1; n <= 10000; ++n) {
        auto r = box.publish(c, n, 3, n);
        if (r.post) ++posts;
      }
    });
    for (auto& worker : workers) worker.join();
    check(posts == 1); // bounded event/memory use even if UI cannot run
    for (auto& f : box.take(3)) check(f && *f == 10000);
    check(box.publish(0, 10001, 3, 10001).post);
    bool invalid = false;
    try { box.publish(4, 1, 3, 0); }
    catch (const std::out_of_range&) { invalid = true; }
    check(invalid);
    std::cout << "independent lanes, coalescing, ordering, restart, concurrency: PASS\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
