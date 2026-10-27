#include "ui/imu_sample_rate.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
static void check(bool v) { if (!v) throw std::runtime_error("IMU measured rate regression"); }
int main() {
  using prism_viewer::ui::ImuSampleRate;
  ImuSampleRate r;
  uint64_t t=1000;uint16_t sequence=65000;
  r.add(t,sequence,true,false,false);
  // Device cadence does not depend on host burst grouping; also wrap uint16.
  for (unsigned i=1;i<10000;++i) {t+=1250;r.add(t,++sequence,true,false,false);}
  check(std::abs(r.rate()-800)<1e-9&&r.sequenceFaults()==0&&r.clockFaults()==0);
  check(r.intervals()==9999&&r.minUs()==1250&&r.maxUs()==1250);
  // Valid PPS re-anchor may move time backward; never use its delta.
  t-=7000;r.add(t,++sequence,true,true,false);
  check(r.rate()==800&&r.clockFaults()==0);
  // No concealing missing, repeated or explicitly gap-marked samples.
  t+=2500;sequence+=2;r.add(t,sequence,true,false,false);
  check(r.rate()==0&&r.sequenceFaults()==1);
  r.add(t,sequence,true,false,false);check(r.sequenceFaults()==2);
  t+=1250;r.add(t,++sequence,true,false,true);check(r.sequenceFaults()==3);
  for (unsigned i=0;i<10;++i) {t+=2000;r.add(t,++sequence,true,false,false);}
  check(r.rate()==500); // Does not force a nominal 800Hz.
  r.add(t-1,++sequence,true,false,false);check(r.rate()==0&&r.clockFaults()==1);
  r.add(0,++sequence,true,false,false);check(r.rate()==0&&r.clockFaults()==2);
  t=100000;r.add(t,++sequence,false,false,false);
  for (unsigned i=0;i<10;++i) {t+=2500;r.add(t,++sequence,false,false,false);}
  check(r.rate()==400);
  r.add(999999999,++sequence,true,false,false);check(r.rate()==0);
  ImuSampleRate p;
  uint64_t anchor=1000000;uint16_t seq=0;
  p.add(anchor,seq,true,false,false);
  // Oscillator produces805 samples/sec but extrapolated intervals say1250us.
  // PPS-to-PPS sample count must win: do not artificially display800Hz.
  for (unsigned second=1;second<=6;++second) {
    for (unsigned i=1;i<805;++i)p.add(anchor+i*1250,++seq,true,false,false);
    anchor+=1000000;p.add(anchor,++seq,true,true,false);
  }
  check(p.anchored()&&p.rate()==805&&p.sequenceFaults()==0&&p.clockFaults()==0);
  p.add(anchor+1000000,++seq,true,true,true);
  check(!p.anchored()&&p.rate()==0&&p.sequenceFaults()==1);
  std::cout << "IMU device cadence, wrap, gaps, clock-domain/re-anchor: PASS\n";
}
