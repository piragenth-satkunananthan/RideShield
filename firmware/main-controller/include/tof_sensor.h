#pragma once
#include <Adafruit_VL53L0X.h>
#include "core.h"

// Initialize before tasks start. During operation only controllerTask uses
// Wire (both this driver and the IMU), so bus transactions cannot interleave.
class TofSensor {
 public:
  struct Sample { uint16_t mm=0;uint8_t status=255;bool valid=false;rs::Ms ms=0; };
  bool begin(uint8_t address) {
    Wire.beginTransmission(address);
    if(Wire.endTransmission()!=0)return false;
    initialized_=sensor_.begin(address,false,&Wire);
    if(initialized_)initialized_=sensor_.startRangeContinuous(100);
    Wire.setClock(400000);Wire.setTimeOut(3);
    return initialized_;
  }
  bool poll(rs::Ms now,Sample& result) {
    if(!initialized_ || now<nextPoll_)return false;
    nextPoll_=now+50;
    // Do not call waitRangeComplete/readRange: they wait for a measurement.
    const bool ready=sensor_.isRangeComplete();
    if(sensor_.Status!=VL53L0X_ERROR_NONE){nextPoll_=now+1000;result={0,255,false,now};return true;}
    if(!ready)return false;
    VL53L0X_RangingMeasurementData_t data{};
    const auto error=sensor_.getRangingMeasurement(&data,false);
    const auto cleared=sensor_.clearInterruptMask(false);
    result={data.RangeMilliMeter,data.RangeStatus,
      error==VL53L0X_ERROR_NONE && cleared==VL53L0X_ERROR_NONE && data.RangeStatus==0,now};
    return true;
  }
  bool initialized() const { return initialized_; }
 private:
  Adafruit_VL53L0X sensor_;
  bool initialized_=false;rs::Ms nextPoll_=0;
};
