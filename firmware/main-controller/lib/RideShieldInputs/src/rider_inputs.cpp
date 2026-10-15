#include "rider_inputs.h"
#include <algorithm>
#include <cmath>

namespace rs {
bool RiderInputs::Debounce::update(bool value,Ms now) {
  if(!initialized){initialized=true;raw=stable=value;since=now;}
  if(value!=raw){raw=value;since=now;}
  if(now>=since && now-since>=50)stable=raw;
  return stable;
}
void RiderInputs::range(uint16_t mm,bool valid,Ms now) {
  if(now<rangeAt_)return;
  if(now-rangeAt_>500){rangeCount_=rangeNext_=0;head_=Presence::Unknown;headPending_=false;}
  rangeAt_=now;rangeValid_=valid && mm>=10 && mm<=2000;
  if(!rangeValid_){rangeCount_=rangeNext_=0;head_=Presence::Unknown;headPending_=false;return;}
  ranges_[rangeNext_]=mm;rangeNext_=(rangeNext_+1)%5;
  if(rangeCount_<5)++rangeCount_;
}
void RiderInputs::alcoholSample(uint16_t mv,bool valid,Ms now) {
  if(now<mqAt_)return;
  mqAt_=now;mqValid_=valid && mv>10 && mv<3100;
  mqMv_=mv;
}
RiderView RiderInputs::tick(Ms now,bool strapClosed,bool irHigh) {
  RiderView v;
  v.strapClosed=strap_.update(strapClosed,now);
  const bool ir=ir_.update(irHigh,now);
  v.tofFresh=rangeValid_ && now>=rangeAt_ && now-rangeAt_<=500;
  if(v.tofFresh){
    uint32_t sum=0;uint16_t lo=65535,hi=0;
    for(unsigned i=0;i<rangeCount_;++i){sum+=ranges_[i];lo=std::min(lo,ranges_[i]);hi=std::max(hi,ranges_[i]);}
    v.rangeMm=rangeCount_?sum/rangeCount_:0;
    v.tofStable=rangeCount_==5 && hi-lo<=20;
  }
  if(!v.tofFresh || !cal_.headReady()){head_=Presence::Unknown;headPending_=false;}
  else {
    const unsigned gap=cal_.emptyMm-cal_.wornMm;
    const unsigned near=cal_.wornMm+gap/3,far=cal_.wornMm+2*gap/3;
    Presence want=v.rangeMm<=near?Presence::Present:v.rangeMm>=far?Presence::Absent:head_;
    if(want!=head_){
      if(!headPending_ || want!=candidate_){candidate_=want;headSince_=now;headPending_=true;}
      if(now-headSince_>=500){head_=want;headPending_=false;}
    } else headPending_=false;
  }
  v.head=head_;
  v.wear=head_==Presence::Unknown?Wear::Unknown:head_==Presence::Absent?Wear::NotWorn:v.strapClosed?Wear::Worn:Wear::StrapOpen;
  // An open strap does not disable monitoring while a head is present.
  if(cal_.eyeReady() && head_==Presence::Present && ir==bool(cal_.irClosed)){
    if(!eyeTiming_){eyeTiming_=true;eyeSince_=now;}
    v.closedMs=now-eyeSince_;v.eyeProlonged=v.closedMs>=2000;
  } else eyeTiming_=false;
  v.mqMv=mqMv_;
  if(!cal_.mqReady())v.alcohol=Alcohol::Uncalibrated;
  else if(now<boot_ || now-boot_<warmupMs_)v.alcohol=Alcohol::Warming;
  else if(!mqValid_ || now<mqAt_ || now-mqAt_>1000)v.alcohol=Alcohol::Unknown;
  else {
    if(mqMv_>=cal_.mqLimitMv){
      if(!mqTiming_){mqTiming_=true;mqSince_=now;}
      if(now-mqSince_>=2000)mqHigh_=true;
    } else {
      mqTiming_=false; // A new high requires a continuous two-second interval.
      if(mqMv_+50<cal_.mqLimitMv)mqHigh_=false;
    }
    v.alcohol=mqHigh_?Alcohol::High:Alcohol::Clear;
  }
  if(v.alcohol==Alcohol::Unknown || v.alcohol==Alcohol::Warming || v.alcohol==Alcohol::Uncalibrated){mqTiming_=false;mqHigh_=false;}
  return v;
}
LocalOutput combineWarnings(const Output& base,const RiderView& rider,Ms now) {
  LocalOutput out{base,warningName(base.warning)};
  if(base.warning==Warning::Emergency || base.warning==Warning::Countdown || base.warning==Warning::System)return out;
  unsigned period=0,on=0;
  if(rider.eyeProlonged){out.label="EYE_SIGNAL";period=700;on=200;}
  else if(rider.head==Presence::Present && rider.alcohol==Alcohol::High){out.label="ALCOHOL_SCREEN";period=2000;on=150;}
  else if(rider.wear==Wear::StrapOpen){out.label="STRAP_OPEN";period=3000;on=100;}
  if(period){const bool pulse=now%period<on;out.pins.left=out.pins.right=out.pins.buzzer=out.pins.led=pulse;}
  return out;
}
const char* presenceName(Presence p){return p==Presence::Present?"PRESENT":p==Presence::Absent?"ABSENT":"UNKNOWN";}
const char* wearName(Wear w){switch(w){case Wear::Worn:return "WORN";case Wear::StrapOpen:return "STRAP_OPEN";case Wear::NotWorn:return "NOT_WORN";default:return "UNKNOWN";}}
const char* alcoholName(Alcohol a){switch(a){case Alcohol::Clear:return "CLEAR";case Alcohol::High:return "HIGH";case Alcohol::Warming:return "WARMING";case Alcohol::Unknown:return "UNKNOWN";default:return "UNCALIBRATED";}}
unsigned eventIndex(const Incident& i){
  if(!std::isfinite(i.peakG)||!std::isfinite(i.peakDps)||i.peakG<=0)return 0;
  const float a=std::clamp((i.peakG-1)/15,0.f,1.f),g=std::clamp(i.peakDps/2000,0.f,1.f);
  return 1+unsigned(std::lround(99*(0.75f*a+0.25f*g)));
}
}
