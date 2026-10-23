#pragma once
#include <cstdint>

namespace prism_viewer {
// Close host credits before STOP. Once Agent releases the capture owner,
// deferred frames still need auditing/recording but must not send STREAM_ACK.
class CaptureCredits {
 public:
  void stopping() noexcept { open_=false; }
  template<class Sender> void acknowledge(uint32_t id,Sender&& sender) const {
    if(open_)sender(id);
  }
 private:
  bool open_=true;
};
}
