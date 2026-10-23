#include "communication/capture_credits.hpp"
#include <stdexcept>
#include <iostream>
static void check(bool yes){if(!yes)throw std::runtime_error("STOP credit regression");}
int main(){
  bool owner=true;unsigned acks=0,errors=0,data=0;
  auto sender=[&](uint32_t){if(owner)++acks;else ++errors;};
  prism_viewer::CaptureCredits credits;
  for(unsigned i=0;i<100;++i){++data;credits.acknowledge(i,sender);}
  check(acks==100&&!errors);
  credits.stopping();owner=false;
  for(unsigned i=100;i<110;++i){++data;credits.acknowledge(i,sender);}
  check(data==110&&acks==100&&!errors);
  sender(110);check(errors==1); // old unconditional post-STOP ACK is rejected
  std::cout<<"PASS preSTOP ACK, postSTOP all data/no ACK, old nonowner negative\n";
}
