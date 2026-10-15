#pragma once
// Copy to local_config.h only after checking the hardware wiring.
// Never commit an actual Wi-Fi password, phone token or emergency contact.
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
#define RS_WIFI_PASSWORD "rideshield-bench-only"
#define RS_PHONE_TOKEN "bench-token-change-before-use-01"
#define RS_SMS_NUMBER ""
