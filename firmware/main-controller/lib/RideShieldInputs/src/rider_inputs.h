#pragma once
#include "core.h"
#include <cstdint>

namespace rs {
// Stored as individually validated fields by the hardware adapter.
struct RiderCalibration {
  uint16_t wornMm=0, emptyMm=0;
  int8_t irOpen=-1, irClosed=-1;
  uint16_t mqLimitMv=0;
  bool headReady() const { return wornMm>=10 && emptyMm<=2000 && emptyMm>=wornMm+30; }
  bool eyeReady() const { return irOpen>=0 && irOpen<=1 && irClosed>=0 && irClosed<=1 && irOpen!=irClosed; }
  bool mqReady() const { return mqLimitMv>=100 && mqLimitMv<=3000; }
};
enum class Wear : uint8_t { Unknown, NotWorn, StrapOpen, Worn };
enum class Alcohol : uint8_t { Uncalibrated, Warming, Unknown, Clear, High };
struct RiderView {
  Presence head=Presence::Unknown;
  Wear wear=Wear::Unknown;
  Alcohol alcohol=Alcohol::Uncalibrated;
  bool tofFresh=false, tofStable=false, strapClosed=false, eyeProlonged=false;
  uint16_t rangeMm=0, mqMv=0;
  Ms closedMs=0;
};
class RiderInputs {
 public:
  explicit RiderInputs(Ms warmupMs=180000):warmupMs_(warmupMs){}
  void begin(Ms now,const RiderCalibration& c) { *this=RiderInputs(warmupMs_);boot_=now;cal_=c; }
  void calibrate(const RiderCalibration& c) { cal_=c;head_=candidate_=Presence::Unknown;headPending_=false;eyeTiming_=false;mqTiming_=false; }
  const RiderCalibration& calibration() const { return cal_; }
  void range(uint16_t mm,bool valid,Ms now);
  void alcoholSample(uint16_t mv,bool valid,Ms now);
  RiderView tick(Ms now,bool strapClosed,bool irHigh);
 private:
  struct Debounce {
    bool initialized=false,raw=false,stable=false;Ms since=0;
    bool update(bool value,Ms now);
  } strap_,ir_;
  RiderCalibration cal_{};Ms boot_=0,warmupMs_;
  uint16_t ranges_[5]{};unsigned rangeCount_=0,rangeNext_=0;Ms rangeAt_=0;
  bool rangeValid_=false,headPending_=false,eyeTiming_=false,mqTiming_=false,mqValid_=false;
  Presence head_=Presence::Unknown,candidate_=Presence::Unknown;
  Ms headSince_=0,eyeSince_=0,mqSince_=0,mqAt_=0;
  uint16_t mqMv_=0;bool mqHigh_=false;
};
struct LocalOutput { Output pins{};const char* label="NONE"; };
LocalOutput combineWarnings(const Output&,const RiderView&,Ms now);
const char* presenceName(Presence);
const char* wearName(Wear);
const char* alcoholName(Alcohol);
// Experimental signal index. Zero means there is no motion-event peak yet.
unsigned eventIndex(const Incident&);
}
