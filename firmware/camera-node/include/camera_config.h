#pragma once
#if __has_include("local_config.h")
#include "local_config.h"
#endif
#ifndef RS_CAMERA_REAR
#error "Select the front or rear PlatformIO environment"
#endif
#ifndef RS_CAMERA_SSID
#define RS_CAMERA_SSID ""
#endif
#ifndef RS_CAMERA_PASSWORD
#define RS_CAMERA_PASSWORD "rideshield-bench-only"
#endif
#ifndef RS_CAMERA_FPS
#define RS_CAMERA_FPS 10
#endif
#ifndef RS_CAMERA_JPEG_QUALITY
#define RS_CAMERA_JPEG_QUALITY 16
#endif
#ifndef RS_CAMERA_HMIRROR
#define RS_CAMERA_HMIRROR 0
#endif
#ifndef RS_CAMERA_VFLIP
#define RS_CAMERA_VFLIP 0
#endif
static_assert(RS_CAMERA_REAR==0 || RS_CAMERA_REAR==1,"Invalid camera role");
static_assert(RS_CAMERA_FPS>=1 && RS_CAMERA_FPS<=15,"FPS must be 1..15");
static_assert(RS_CAMERA_JPEG_QUALITY>=10 && RS_CAMERA_JPEG_QUALITY<=40,"JPEG quality must be 10..40");
namespace board {
// AI-Thinker ESP32-CAM + OV2640. Verify the pinout for other board variants.
constexpr int PWDN=32,RESET=-1,XCLK=0,SIOD=26,SIOC=27;
constexpr int D0=5,D1=18,D2=19,D3=21,D4=36,D5=39,D6=34,D7=35;
constexpr int VSYNC=25,HREF=23,PCLK=22,FLASH=4;
constexpr const char* NODE=RS_CAMERA_REAR?"REAR":"FRONT";
constexpr const char* HOST=RS_CAMERA_REAR?"rideshield-rear":"rideshield-front";
constexpr int IP_LAST=RS_CAMERA_REAR?3:2;
constexpr unsigned FRAME_PERIOD_MS=1000/RS_CAMERA_FPS;
constexpr unsigned MAX_FRAME_AGE_MS=250,SEND_BUDGET_MS=250;
}
