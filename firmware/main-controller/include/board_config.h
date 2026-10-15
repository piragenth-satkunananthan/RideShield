#pragma once
#if __has_include("local_config.h")
#include "local_config.h"
#endif
#ifndef RS_BENCH
#define RS_BENCH 0
#endif
#ifndef RS_ENABLE_OUTPUTS
#define RS_ENABLE_OUTPUTS 0
#endif
#ifndef RS_ENABLE_GPS
#define RS_ENABLE_GPS 0
#endif
#ifndef RS_ENABLE_SD
#define RS_ENABLE_SD 0
#endif
#ifndef RS_ENABLE_LTE
#define RS_ENABLE_LTE 0
#endif
#ifndef RS_ALLOW_SMS
#define RS_ALLOW_SMS 0
#endif
#ifndef RS_WIFI_PASSWORD
#define RS_WIFI_PASSWORD "rideshield-bench-only"
#endif
#ifndef RS_PHONE_TOKEN
#define RS_PHONE_TOKEN "bench-token-change-before-use-01"
#endif
#ifndef RS_SMS_NUMBER
#define RS_SMS_NUMBER ""
#endif

// Provisional NodeMCU-32S GPIO allocation. Verify the exact board and every
// breakout before enabling optional peripherals or wiring high-current loads.
// GPIO34 is input-only and is intentionally used as UART RX, never as TX.
namespace board {
constexpr int SDA=21,SCL=22,MPU_ADDRESS=0x68;
constexpr int CANCEL=32,SOS=33;
constexpr int BUZZER=25,LEFT_MOTOR=26,RIGHT_MOTOR=-1,LED=27;
constexpr int GPS_RX=16,GPS_TX=17,GPS_BAUD=9600;
constexpr int LTE_RX=34,LTE_TX=13,LTE_BAUD=115200;
constexpr int SD_SCK=18,SD_MOSI=23,SD_MISO=19,SD_CS=14;
constexpr const char* JOURNAL=RS_BENCH?"rs-bench":"rs-hardware";
}
static_assert(sizeof(RS_PHONE_TOKEN)==33,"Phone token must have exactly 32 ASCII bytes");
static_assert(sizeof(RS_WIFI_PASSWORD)>=9&&sizeof(RS_WIFI_PASSWORD)<=64,"AP password must be 8..63 characters");
static_assert(!RS_ALLOW_SMS||(RS_ENABLE_LTE&&!RS_BENCH),"SMS requires LTE and hardware profile");
static_assert(!RS_ALLOW_SMS||(sizeof(RS_SMS_NUMBER)>=10&&sizeof(RS_SMS_NUMBER)<=17),"Configure an E.164 recipient before enabling SMS");
constexpr bool validBoardPins(){
 constexpr int pins[]={board::SDA,board::SCL,board::CANCEL,board::SOS,
 RS_ENABLE_OUTPUTS?board::BUZZER:-1,RS_ENABLE_OUTPUTS?board::LEFT_MOTOR:-1,
 RS_ENABLE_OUTPUTS?board::RIGHT_MOTOR:-1,RS_ENABLE_OUTPUTS?board::LED:-1,
 RS_ENABLE_GPS?board::GPS_RX:-1,RS_ENABLE_GPS?board::GPS_TX:-1,
 RS_ENABLE_LTE?board::LTE_RX:-1,RS_ENABLE_LTE?board::LTE_TX:-1,
 RS_ENABLE_SD?board::SD_SCK:-1,RS_ENABLE_SD?board::SD_MOSI:-1,
 RS_ENABLE_SD?board::SD_MISO:-1,RS_ENABLE_SD?board::SD_CS:-1};
 for(unsigned i=0;i<sizeof(pins)/sizeof(pins[0]);++i){
  int p=pins[i];if(p<0)continue;
  // GPIO6..11 are wired to flash; GPIO0/2/5/12/15 affect boot; 1/3 are USB
  // serial UART on this board. Avoid these pins in the initial design.
  if(p>39||p==0||p==1||p==2||p==3||p==5||p==12||p==15||(p>=6&&p<=11))return false;
  for(unsigned j=0;j<i;++j)if(p==pins[j])return false;
 }return true;
}
static_assert(validBoardPins(),"Duplicate/reserved GPIO assignment");
static_assert(!RS_ENABLE_OUTPUTS||(board::BUZZER<34&&board::LEFT_MOTOR<34&&board::LED<34&&board::RIGHT_MOTOR<34),"Input-only GPIO cannot drive outputs");
static_assert(!RS_ENABLE_GPS||board::GPS_TX<34,"Input-only GPIO cannot transmit UART");
static_assert(!RS_ENABLE_LTE||board::LTE_TX<34,"Input-only GPIO cannot transmit UART");
static_assert(!RS_ENABLE_SD||(board::SD_SCK<34&&board::SD_MOSI<34&&board::SD_CS<34),"Input-only GPIO cannot drive SPI");
