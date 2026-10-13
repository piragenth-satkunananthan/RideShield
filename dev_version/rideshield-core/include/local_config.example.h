#pragma once
// Copy to local_config.h. Keep RS_ALLOW_SMS=0 for bench testing.
// Verify pins in board_config.h against the exact board first.
#define RS_ENABLE_OUTPUTS 0
#define RS_ENABLE_GPS 0
#define RS_ENABLE_SD 0
#define RS_ENABLE_LTE 0
#define RS_ALLOW_SMS 0
#define RS_WIFI_PASSWORD "rideshield-bench-only"
#define RS_PHONE_TOKEN "bench-token-change-before-use-01"
#define RS_SMS_NUMBER ""
