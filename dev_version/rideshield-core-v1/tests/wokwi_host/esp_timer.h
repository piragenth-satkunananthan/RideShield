#pragma once
#include <cstdint>
extern uint64_t fakeMs;
inline int64_t esp_timer_get_time(){return int64_t(fakeMs*1000);}
