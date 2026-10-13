#include "blackbox.h"
namespace rs {
BlackBox::BlackBox(MotionRecord*h,size_t hc,MotionRecord*e,size_t ec,Ms post,bool limited):history_(h),event_(e),historyCapacity_(hc),eventCapacity_(ec),postMs_(post),limited_(limited){if(h&&e&&hc&&ec)status_=Status::Idle;}
void BlackBox::push(const Motion&m,uint32_t flags){
 if(status_==Status::Unavailable)return;
 history_[head_]={m,flags};head_=(head_+1)%historyCapacity_;if(historyCount_<historyCapacity_)++historyCount_;
 if(status_==Status::Recording&&m.ms<=until_){if(count_<eventCapacity_)event_[count_++]={m,flags};else truncated_=true;}
}
bool BlackBox::trigger(Id id,Ms now){
 if(status_!=Status::Idle){++missed_;return false;}id_=id;count_=0;truncated_=limited_;until_=now+postMs_;
 size_t first=(head_+historyCapacity_-historyCount_)%historyCapacity_;
 for(size_t i=0;i<historyCount_;++i){const auto&r=history_[(first+i)%historyCapacity_];if(r.motion.ms>now||now-r.motion.ms>10000)continue;if(count_<eventCapacity_)event_[count_++]=r;else truncated_=true;}
 status_=Status::Recording;return true;
}
void BlackBox::tick(Ms now){if(status_==Status::Recording&&now>=until_)status_=Status::Ready;}
bool BlackBox::acquire(const MotionRecord*&r,size_t&n,Id&id,bool&t){if(status_!=Status::Ready)return false;r=event_;n=count_;id=id_;t=truncated_;status_=Status::Exporting;return true;}
void BlackBox::release(){if(status_==Status::Exporting)status_=Status::Idle;}
}
