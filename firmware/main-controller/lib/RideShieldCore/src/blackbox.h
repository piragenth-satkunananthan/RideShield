#pragma once
#include "core.h"
namespace rs {
struct MotionRecord{Motion motion{};uint32_t flags=0;};
class BlackBox{
public:
 enum class Status{Unavailable,Idle,Recording,Ready,Exporting};
 BlackBox(MotionRecord*,size_t,MotionRecord*,size_t,Ms=30000,bool limited=false);
 void push(const Motion&,uint32_t=0);bool trigger(Id,Ms);void tick(Ms);
 bool acquire(const MotionRecord*&,size_t&,Id&,bool&);void release();
 Status status()const{return status_;}uint32_t missedCaptures()const{return missed_;}
private:
 MotionRecord *history_,*event_;size_t historyCapacity_,eventCapacity_,head_=0,historyCount_=0,count_=0;
 Ms until_=0,postMs_;Id id_{};Status status_=Status::Unavailable;bool truncated_=false,limited_=false;uint32_t missed_=0;
};
}
