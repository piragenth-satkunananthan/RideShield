#pragma once
#include "core.h"
namespace rs {
uint32_t crc32(const uint8_t*,size_t);uint32_t read32(const uint8_t*);uint64_t read64(const uint8_t*);
void write32(uint8_t*,uint32_t);void write64(uint8_t*,uint64_t);
constexpr size_t CHECKPOINT_SIZE=72,EVENT_SIZE=48,MOTION_SIZE=48;
void encodeCheckpoint(const Checkpoint&,uint8_t*);bool decodeCheckpoint(const uint8_t*,size_t,Checkpoint&);
void encodeEvent(const Event&,uint8_t*);void encodeMotion(const Motion&,uint32_t,uint8_t*);bool validRecord(const uint8_t*,size_t);
}
