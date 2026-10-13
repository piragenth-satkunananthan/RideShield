#include "core.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
namespace rs {
bool operator==(const Id&a,const Id&b){return a.device==b.device&&a.boot==b.boot&&a.sequence==b.sequence;}
Core::Core(Config cfg):cfg_(cfg){}
void Core::begin(Ms now,uint32_t device,uint32_t boot){*this=Core(cfg_);bootMs_=now_=now;device_=device;boot_=boot;log(Kind::Boot,now);mark();}
bool Core::Button::update(bool value,Ms now,Ms debounce,Ms hold){
 if(!initialized){initialized=true;raw=stable=value;changedMs=pressedMs=now;armed=!value;return false;}
 if(value!=raw){raw=value;changedMs=now;}
 if(raw!=stable&&now-changedMs>=debounce){stable=raw;if(!stable){armed=true;fired=false;}else pressedMs=now;}
 if(stable&&raw&&armed&&!fired&&now-pressedMs>=hold){fired=true;return true;}
 return false;
}
void Core::Button::requireRelease(){if(raw||stable)armed=false;fired=false;}
void Core::log(Kind kind,Ms now,float value,uint32_t detail){
 if(eventCount_==events_.size()){++dropped_;return;}
 events_[(eventHead_+eventCount_)%events_.size()]={now,incident_.id,kind,value,detail};++eventCount_;
}
bool Core::popEvent(Event&e){if(!eventCount_)return false;e=events_[eventHead_];eventHead_=(eventHead_+1)%events_.size();--eventCount_;return true;}
void Core::head(Presence value,Ms now){if(now<headAt_)return;head_=value;headAt_=now;if(value==Presence::Present){everPresent_=true;lastPresent_=now;}}
bool Core::confidentlyUnworn(Ms now)const{return head_==Presence::Absent&&now>=headAt_&&now-headAt_<=cfg_.headStaleMs&&(!everPresent_||now-lastPresent_>cfg_.recentHeadMs);}
void Core::gps(const GpsFix&fix,Ms now){
 if(!fix.valid||fix.ms>now||!std::isfinite(fix.lat)||!std::isfinite(fix.lon)||fix.lat< -90||fix.lat>90||fix.lon< -180||fix.lon>180||(fix_.valid&&fix.ms<fix_.ms))return;
 fix_=fix;
}
void Core::vision(const Vision&v,bool rear){
 auto&old=rear?rear_:front_;auto&since=rear?rearWarningAt_:frontWarningAt_;
 if(!old.valid||old.expiresMs<=now_||old.pothole!=v.pothole||old.wet!=v.wet||old.vehicle!=v.vehicle||old.zone!=v.zone)since=now_;
 old=v;
}
void Core::clearToMonitor(){state_=imuHealthy_?State::Monitor:State::Degraded;above_=0;resetStillness();mark();}
void Core::resetStillness(){stillN_=0;stillMean_=stillM2_=0;stillMaxW_=0;}
bool Core::stillness(Ms now,float a,float w){
 if(stillN_&&now-stillLast_>cfg_.imuGapMs)resetStillness();
 if(w>10||a<0.8f||a>1.2f){resetStillness();return false;}
 if(!stillN_)stillStart_=now;
 stillLast_=now;++stillN_;double delta=a-stillMean_;stillMean_+=delta/stillN_;stillM2_+=delta*(a-stillMean_);stillMaxW_=std::max(stillMaxW_,w);
 return now-stillStart_>=1000&&stillN_>=150&&std::sqrt(stillM2_/(stillN_-1))<=0.1&&stillMaxW_<=10;
}
void Core::startCandidate(Ms now,float a,float w,bool clipped){
 incident_={};incident_.id={device_,boot_,++counter_};incident_.startedMs=now;incident_.peakG=a;incident_.peakDps=w;
 incident_.clipped=clipped;incident_.clippedSamples=clipped?1:0;send_=SendState::None;attempt_=0;state_=State::Verify;resetStillness();
 log(Kind::Candidate,now,a);mark();
 if(confidentlyUnworn(now)){log(Kind::Handling,now,a);clearToMonitor();return;}
 if(a>=cfg_.directG||clipped)startCountdown(now);
}
void Core::startCountdown(Ms now){state_=State::Countdown;countdownAt_=now;cancel_.requireRelease();log(Kind::Countdown,now,incident_.peakG);mark();}
void Core::emergency(Ms now,bool manual){
 if(state_==State::Incident){if(manual&&!incident_.manual){incident_.manual=true;mark();}incident_.localAcknowledged=false;emergencyAt_=now;return;}
 if(state_!=State::Verify&&state_!=State::Countdown){incident_={};incident_.id={device_,boot_,++counter_};incident_.startedMs=now;}
 incident_.manual=incident_.manual||manual;state_=State::Incident;emergencyAt_=now;send_=SendState::Queued;retryAt_=now;attempt_=0;
 log(Kind::Emergency,now,incident_.peakG,manual?1:0);mark();
}
bool Core::motion(const Motion&s,Ms now){
 if(now<now_)return false;
 const float axes[]={s.ax,s.ay,s.az,s.gx,s.gy,s.gz};bool finite=true;
 for(unsigned i=0;i<6;++i)finite=finite&&std::isfinite(axes[i])&&std::fabs(axes[i])<=(i<3?16.01f:2001.0f);
 if(!s.valid||!finite||s.ms>now||now-s.ms>cfg_.imuGapMs){imuUnavailable(now);return false;}
 if(hadMotion_&&s.ms<=lastMotion_)return false;
 if(hadMotion_&&s.ms-lastMotion_>cfg_.imuGapMs){
  ++gaps_;above_=0;resetStillness();log(Kind::ImuGap,now);
  if(state_==State::Verify||state_==State::Countdown||state_==State::Incident)++incident_.sampleGaps;
 }
 lastMotion_=s.ms;hadMotion_=true;
 if(!imuHealthy_){imuHealthy_=true;log(Kind::ImuRestored,now);}
 if(state_==State::Degraded){state_=State::Monitor;mark();}
 const float a=std::sqrt(s.ax*s.ax+s.ay*s.ay+s.az*s.az),w=std::sqrt(s.gx*s.gx+s.gy*s.gy+s.gz*s.gz);
 bool clipped=false;for(int i=0;i<6;++i)clipped=clipped||std::fabs(axes[i])>=(i<3?15.68f:1960.0f);
 if(state_==State::Monitor){
  above_=a>=cfg_.candidateG?above_+1:0;
  if(a>=cfg_.directG||clipped||above_>=2){startCandidate(now,a,w,clipped);if(state_==State::Verify&&w>=cfg_.supportDps)startCountdown(now);}
 }else{
  incident_.peakG=std::max(incident_.peakG,a);incident_.peakDps=std::max(incident_.peakDps,w);incident_.clipped=incident_.clipped||clipped;
  if(clipped)++incident_.clippedSamples;
  if(state_==State::Verify&&(a>=cfg_.directG||clipped||w>=cfg_.supportDps||stillness(s.ms,a,w)))startCountdown(now);
  if(state_==State::Incident&&a>=cfg_.directG&&now-lastImpactEvent_>=1000){log(Kind::AdditionalImpact,now,a);lastImpactEvent_=now;}
 }
 return true;
}
void Core::imuUnavailable(Ms now){
 if(imuHealthy_){imuHealthy_=false;log(Kind::ImuLost,now);}above_=0;resetStillness();
 if(state_==State::Monitor){state_=State::Degraded;mark();}
 else if(state_==State::Verify){incident_.uncertain=true;startCountdown(now);}
 else if(state_==State::Countdown||state_==State::Incident){if(!incident_.uncertain){incident_.uncertain=true;mark();}}
}
void Core::tick(Ms now,bool cancelPressed,bool sosPressed){
 if(now<now_)return;
 now_=now;bool sos=sos_.update(sosPressed,now,cfg_.buttonDebounceMs,cfg_.sosHoldMs);
 bool cancel=cancel_.update(cancelPressed,now,cfg_.buttonDebounceMs,cfg_.cancelHoldMs);
 if(sos)emergency(now,true);
 if(!sos&&cancel){if(state_==State::Countdown){log(Kind::Cancelled,now);send_=SendState::None;clearToMonitor();}else if(state_==State::Incident)acknowledge(now);}
 if((hadMotion_&&now-lastMotion_>cfg_.imuFaultMs)||(!hadMotion_&&now-bootMs_>cfg_.imuFaultMs))imuUnavailable(now);
 if(state_==State::Verify&&now-incident_.startedMs>=cfg_.verifyMs){log(Kind::Rejected,now,incident_.peakG);clearToMonitor();}
 if(state_==State::Countdown&&now-countdownAt_>=cfg_.countdownMs)emergency(now,false);
 if(send_==SendState::Attempting&&now-attemptAt_>=90000)smsResult(incident_.id,attempt_,SendResult::Unknown,now);
}
Ms Core::countdownRemaining(Ms now)const{if(state_!=State::Countdown||now>=countdownAt_+cfg_.countdownMs)return 0;return countdownAt_+cfg_.countdownMs-now;}
bool Core::takeSms(Ms now,SmsJob&job){
 if(state_!=State::Incident||now<retryAt_||!(send_==SendState::Queued||send_==SendState::Failed||send_==SendState::Unknown))return false;
 bool duplicate=send_==SendState::Unknown||incident_.possibleDuplicate;
 job={};job.id=incident_.id;job.attempt=++attempt_;char id[32],location[75];formatId(job.id,id,sizeof(id));
 if(!fix_.valid||fix_.ms>now)std::snprintf(location,sizeof(location),"LOCATION UNAVAILABLE");
 else if(now-fix_.ms<=cfg_.gpsFreshMs)std::snprintf(location,sizeof(location),"LOC %.5f,%.5f age %llus",fix_.lat,fix_.lon,static_cast<unsigned long long>((now-fix_.ms)/1000));
 else std::snprintf(location,sizeof(location),"OLD LOC %.5f,%.5f age %llus",fix_.lat,fix_.lon,static_cast<unsigned long long>((now-fix_.ms)/1000));
 std::snprintf(job.text,sizeof(job.text),"TEST RS-%s %s%s; %s. Contact rider.",id,incident_.manual?"SOS":"SUSPECTED EVENT",duplicate?" RETRY?":"",location);
 send_=SendState::Attempting;attemptAt_=now;log(Kind::SmsAttempt,now,0,attempt_);mark();return true;
}
bool Core::smsResult(Id id,uint32_t attempt,SendResult result,Ms now){
 if(!(id==incident_.id)||attempt!=attempt_||send_!=SendState::Attempting)return false;
 switch(result){
 case SendResult::Submitted:send_=SendState::Submitted;log(Kind::SmsSubmitted,now);break;
 case SendResult::Disabled:send_=SendState::Disabled;log(Kind::SmsDisabled,now);break;
 case SendResult::Failed:send_=SendState::Failed;log(Kind::SmsFailed,now);break;
 case SendResult::Unknown:send_=SendState::Unknown;incident_.possibleDuplicate=true;log(Kind::SmsUnknown,now);break;
 }
 static constexpr Ms backoff[]={15000,30000,60000,120000,300000};unsigned index=std::min(attempt_?attempt_-1:0u,4u);
 retryAt_=now+backoff[index]+((incident_.id.boot^attempt_*2654435761u)%1000);mark();return true;
}
void Core::acknowledge(Ms now){if(state_!=State::Incident)return;incident_.localAcknowledged=true;log(Kind::Acknowledged,now);mark();}
bool Core::closeIncident(Ms now){if(state_!=State::Incident||(send_!=SendState::Submitted&&send_!=SendState::Disabled))return false;log(Kind::Closed,now);send_=SendState::None;clearToMonitor();return true;}
Checkpoint Core::checkpoint()const{return{state_,send_,incident_};}
void Core::restore(const Checkpoint&v,Ms now){
 if((v.state!=State::Countdown&&v.state!=State::Incident)||!v.incident.id.sequence)return;
 incident_=v.incident;incident_.recovered=incident_.uncertain=true;incident_.startedMs=now;incident_.localAcknowledged=false;attempt_=0;log(Kind::Recovered,now);
 if(v.state==State::Countdown)startCountdown(now);
 else{state_=State::Incident;emergencyAt_=now;send_=v.send==SendState::Submitted?SendState::Submitted:v.send==SendState::Disabled?SendState::Disabled:SendState::Unknown;
  if(send_==SendState::Unknown)incident_.possibleDuplicate=true;
  retryAt_=now;mark();}
}
Output Core::output(Ms now)const{
 Output o;bool front=front_.valid&&now<front_.expiresMs,rear=rear_.valid&&now<rear_.expiresMs;
 if(state_==State::Incident&&!incident_.localAcknowledged)o.warning=Warning::Emergency;
 else if(state_==State::Countdown)o.warning=Warning::Countdown;
 else if(!imuHealthy_||systemFault_||send_==SendState::Failed||send_==SendState::Unknown)o.warning=Warning::System;
 else if(front&&front_.pothole&&front_.wet)o.warning=Warning::RoadCombined;
 else if(front&&front_.pothole)o.warning=Warning::Pothole;
 else if(front&&front_.wet)o.warning=Warning::WetRoad;
 else if(rear&&rear_.vehicle)o.warning=Warning::Rear;
 bool pulse=false;
 switch(o.warning){
 case Warning::Emergency:pulse=(now-emergencyAt_)%600<250;break;
 case Warning::Countdown:pulse=(now-countdownAt_)%1000<150;break;
 case Warning::System:pulse=now%5000<100;break;
 case Warning::RoadCombined:case Warning::Pothole:pulse=(now-frontWarningAt_)%5000<250;break;
 case Warning::WetRoad:pulse=(now-frontWarningAt_)%5000<150;break;
 case Warning::Rear:pulse=(now-rearWarningAt_)%5000<150;break;
 case Warning::None:break;}
 o.left=o.right=pulse;
 if(o.warning==Warning::Rear&&cfg_.allowDirectional&&rear_.geometryValid){if(rear_.zone==Zone::Left)o.right=false;if(rear_.zone==Zone::Right)o.left=false;}
 o.buzzer=pulse&&(o.warning==Warning::Emergency||o.warning==Warning::Countdown);o.led=o.warning==Warning::None?now%2000<50:pulse;return o;
}
void formatId(const Id&id,char*out,size_t n){std::snprintf(out,n,"%08lx-%08lx-%04lx",static_cast<unsigned long>(id.device),static_cast<unsigned long>(id.boot),static_cast<unsigned long>(id.sequence));}
bool validPhoneNumber(const char*p){if(!p||*p!='+')return false;size_t n=std::strlen(p);if(n<9||n>16||p[1]=='0')return false;for(size_t i=1;i<n;++i)if(p[i]<'0'||p[i]>'9')return false;return true;}
const char* stateName(State s){switch(s){case State::Monitor:return "MONITOR";case State::Degraded:return "DEGRADED";case State::Verify:return "VERIFY";case State::Countdown:return "COUNTDOWN";case State::Incident:return "INCIDENT";}return "INVALID";}
const char* sendName(SendState s){switch(s){case SendState::None:return "NONE";case SendState::Queued:return "QUEUED";case SendState::Attempting:return "ATTEMPTING";case SendState::Submitted:return "SUBMITTED_TO_NETWORK";case SendState::Failed:return "FAILED";case SendState::Unknown:return "UNKNOWN";case SendState::Disabled:return "DISABLED";}return "INVALID";}
const char* warningName(Warning s){switch(s){case Warning::None:return "NONE";case Warning::Rear:return "REAR";case Warning::WetRoad:return "WET_ROAD";case Warning::Pothole:return "POTHOLE";case Warning::RoadCombined:return "ROAD_COMBINED";case Warning::System:return "SYSTEM";case Warning::Countdown:return "COUNTDOWN";case Warning::Emergency:return "EMERGENCY";}return "INVALID";}
const char* kindName(Kind k){switch(k){
#define K(n) case Kind::n:return #n
 K(Boot);K(Candidate);K(Handling);K(Rejected);K(Countdown);K(Cancelled);K(Emergency);K(ImuLost);K(ImuRestored);K(ImuGap);K(SmsAttempt);K(SmsSubmitted);K(SmsFailed);K(SmsUnknown);K(SmsDisabled);K(Acknowledged);K(Closed);K(Recovered);K(AdditionalImpact);
#undef K
 }return "INVALID";}
}
