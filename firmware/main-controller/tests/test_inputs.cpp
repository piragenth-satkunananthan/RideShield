#include "rider_inputs.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace rs;
RiderCalibration cal{40,160,0,1,1000};
RiderView advance(RiderInputs& r,Ms from,Ms to,uint16_t mm=40,bool strap=true,bool ir=false,uint16_t mv=500){
 RiderView v;for(Ms t=from;t<=to;t+=50){r.range(mm,true,t);r.alcoholSample(mv,true,t);v=r.tick(t,strap,ir);}return v;
}
int main(){
 RiderInputs r(0);r.begin(0,{});auto v=advance(r,0,1000);
 assert(v.head==Presence::Unknown&&v.alcohol==Alcohol::Uncalibrated&&!v.eyeProlonged);
 r.begin(0,cal);v=advance(r,0,1000);assert(v.head==Presence::Present&&v.wear==Wear::Worn&&v.tofStable);
 v=advance(r,1050,2000,100);assert(v.head==Presence::Present); // hysteresis
 v=advance(r,2050,3000,160);assert(v.head==Presence::Absent);
 r.range(0,false,3050);v=r.tick(3050,true,false);assert(v.head==Presence::Unknown&&!v.tofFresh);
 v=advance(r,3100,4100);assert(v.head==Presence::Present);
 v=r.tick(4601,true,false);assert(v.head==Presence::Unknown);
 r.range(160,true,4700);v=r.tick(4700,true,false);assert(!v.tofStable); // old window discarded
 r.begin(0,cal);advance(r,0,1000);v=r.tick(1010,false,false);assert(v.strapClosed);
 v=r.tick(1059,false,false);assert(v.strapClosed);v=r.tick(1060,false,false);assert(!v.strapClosed&&v.wear==Wear::StrapOpen);
 r.begin(0,cal);advance(r,0,1000);v=advance(r,1050,3050,40,false,true);assert(!v.eyeProlonged);
 v=advance(r,3100,3150,40,false,true);assert(v.eyeProlonged); // open strap does not disable IR
 advance(r,3200,3300,40,false,false);v=advance(r,3350,4000,40,true,true);assert(!v.eyeProlonged);
 r.begin(0,cal);advance(r,0,1000);v=advance(r,1050,3000,40,true,false,1100);assert(v.alcohol==Alcohol::Clear);
 v=advance(r,3050,3100,40,true,false,1100);assert(v.alcohol==Alcohol::High);
 v=advance(r,3150,3200,40,true,false,980);assert(v.alcohol==Alcohol::High);
 v=advance(r,3250,3300,40,true,false,900);assert(v.alcohol==Alcohol::Clear);
 v=r.tick(4301,true,false);assert(v.alcohol==Alcohol::Unknown);
 r.begin(0,cal);advance(r,0,1000,40,true,false,1100);advance(r,1050,1500,40,true,false,980);
 v=advance(r,1550,2500,40,true,false,1100);assert(v.alcohol==Alcohol::Clear); // threshold must be sustained
 RiderInputs warm(3000);warm.begin(0,cal);v=advance(warm,0,2950,40,true,false,1100);assert(v.alcohol==Alcohol::Warming);
 v=advance(warm,3000,4950,40,true,false,1100);assert(v.alcohol==Alcohol::Clear);v=advance(warm,5000,5050,40,true,false,1100);assert(v.alcohol==Alcohol::High);
 RiderView alert;alert.eyeProlonged=true;alert.wear=Wear::StrapOpen;
 for(auto w:{Warning::Emergency,Warning::Countdown,Warning::System}){Output base{w,true,false,true,false};auto o=combineWarnings(base,alert,100);assert(o.pins.warning==w&&o.pins.left&&!o.pins.right);}
 assert(!strcmp(combineWarnings({},alert,0).label,"EYE_SIGNAL"));
 Incident i;assert(eventIndex(i)==0);i.peakG=16;i.peakDps=2000;assert(eventIndex(i)==100);
 std::cout<<"PASS rider inputs: calibration, freshness, hysteresis, debounce, timing, warmup, output priority\n";
}
