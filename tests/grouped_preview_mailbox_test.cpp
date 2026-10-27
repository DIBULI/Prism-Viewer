#include "ui/grouped_preview_mailbox.hpp"
#include <iostream>
#include <thread>
#include <stdexcept>
using prism_viewer::ui::GroupedPreviewMailbox;
using prism_viewer::ui::PreviewGroupKey;
#define CHECK(x) do { if (!(x)) throw std::runtime_error("line " + std::to_string(__LINE__) + ": " #x); } while (0)
int main() {
  try {
    GroupedPreviewMailbox<int> box;
    box.reset(1);
    auto a=box.arrive({10,100,true},1,15,0);
    CHECK(a.token && !a.post);
    for (unsigned c : {2u,0u,3u}) CHECK(!box.publish(a.token,1,c,10+c));
    CHECK(box.arrive({10,100,true},1,15,10).token==a.token);
    CHECK(!box.arrive({10,101,true},1,15,11).token); // same ID, different clock: never join
    CHECK(box.publish(a.token,1,1,11));
    auto d=box.take(1);
    for(unsigned c=0;c<4;++c) CHECK(d.frames[c] && *d.frames[c]==int(10+c));
    CHECK(!d.warnings && !box.presented(1));
    CHECK(!box.publish(a.token,1,0,99)); // duplicate, no UI callback
    auto b=box.arrive({11,200,true},1,15,20);
    CHECK(!box.publish(b.token,1,2,22));
    auto next=box.arrive({12,300,true},1,15,30);
    CHECK(next.post); d=box.take(1);
    CHECK(d.warnings==1 && d.unfinished==11 && d.frames[2] && *d.frames[2]==22);
    CHECK(!d.frames[0] && !d.frames[1] && !d.frames[3]);
    CHECK(!box.presented(1));
    CHECK(!box.publish(b.token,1,0,20)); // late decode cannot fill next group
    CHECK(!box.arrive({11,200,true},1,15,40).token); // retired trigger cannot reopen
    for(unsigned c=0;c<4;++c) box.publish(next.token,1,c,30+c);
    auto pending=box.arrive({13,400,true},1,15,45); // UI did not drain previous set
    CHECK(!pending.post); d=box.take(1);
    CHECK(d.warnings==2 && d.unfinished==0 && *d.frames[0]==30);
    CHECK(!box.presented(1));
    CHECK(!box.publish(pending.token,1,0,40));
    CHECK(!box.expire(1,1044)); CHECK(box.expire(1,1045));
    d=box.take(1); CHECK(d.warnings==3 && d.unfinished==14 && *d.frames[0]==40);
    CHECK(!box.presented(1)); CHECK(!box.expire(1,9999));
    // A device time adjustment is not a monotonically ordered frame counter.
    CHECK(box.arrive({14,50,true},1,15,1050).token);
    box.reset(2);
    CHECK(!box.publish(pending.token,1,3,99) && !box.expire(1,10000));
    CHECK(!box.presented(1));
    // Reset must clear a pending warning and queued stale callbacks.
    CHECK(!box.expire(2,10000));
    CHECK(box.take(1).warnings==0);
    // Legacy shared frame IDs wrap, but a late set is not a new set.
    auto legacy=box.arrive({0xffffffff,0,false},2,15,0);
    CHECK(legacy.token); CHECK(box.arrive({0,0,false},2,15,1).token);
    CHECK(!box.arrive({0xfffffffe,0,false},2,15,2).token);
    // All16 missing-lane masks: only available lanes are released at deadline.
    for(unsigned mask=0;mask<16;++mask) {
      box.reset(3);
      auto group=box.arrive({1,1000,true},3,15,0);
      for(unsigned c=0;c<4;++c) if(mask&(1u<<c)) box.publish(group.token,3,c,100+c);
      box.expire(3,1000); d=box.take(3);
      CHECK(d.warnings==(mask==15?0u:1u));
      for(unsigned c=0;c<4;++c) CHECK(bool(d.frames[c])==bool(mask&(1u<<c)));
      CHECK(!box.presented(3));
    }
    // Concurrent workers complete one group; exactly one callback is posted.
    for(unsigned round=0;round<500;++round) {
      box.reset(4);
      auto group=box.arrive({round,round+1,true},4,15,0);
      std::array<bool,4> posts{};
      std::array<std::thread,4> workers;
      for(unsigned c=0;c<4;++c) workers[c]=std::thread([&,c] {
        posts[c]=box.publish(group.token,4,c,round*4+c);
      });
      for(auto& worker:workers) worker.join();
      CHECK(unsigned(posts[0])+posts[1]+posts[2]+posts[3]==1);
      d=box.take(4);
      for(unsigned c=0;c<4;++c) CHECK(d.frames[c] && *d.frames[c]==int(round*4+c));
      CHECK(!box.presented(4));
    }
    // UI coalescing stays bounded: a stalled UI takes only the latest whole set.
    box.reset(5);
    for(unsigned f=1;f<=10000;++f) {
      auto group=box.arrive({f,f,true},5,15,f);
      for(unsigned c=0;c<4;++c) box.publish(group.token,5,c,f);
    }
    d=box.take(5); CHECK(d.warnings==9999);
    for(auto& frame:d.frames) CHECK(frame && *frame==10000);
    CHECK(!box.presented(5));
    std::cout << "grouped preview barrier PASS\n";
    return 0;
  } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
