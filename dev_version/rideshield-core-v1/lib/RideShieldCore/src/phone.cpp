#include "phone.h"
#include "codec.h"
#include <algorithm>
#include <cstring>
namespace rs {
bool decodePacket(const uint8_t*b,size_t n,Packet&p){
 if(!b||n!=64||std::memcmp(b,"RSW1",4)||b[4]<1||b[4]>4||b[5]>2||(b[6]&0xf0)||(b[7]&0xf8)||b[28]>3||b[29]>100||b[30]||b[31])return false;
 Packet o;o.type=PacketType(b[4]);o.source=Source(b[5]);o.flags=b[6];o.hazards=b[7];o.session=read32(b+8);o.sequence=read32(b+12);o.phoneMs=read64(b+16);o.auxiliary=read32(b+24);o.zone=Zone(b[28]);o.confidence=b[29];std::memcpy(o.token.data(),b+32,32);
 if(!o.session||!o.sequence||o.phoneMs>(1ULL<<52))return false;
 if(o.type==PacketType::Observation){if(o.source==Source::None||o.auxiliary>10000||(o.source==Source::Front&&((o.hazards&4)||o.zone!=Zone::Unknown))||(o.source==Source::Rear&&(o.hazards&3)))return false;}
 else if(o.source!=Source::None||o.flags||o.hazards||o.zone!=Zone::Unknown||o.confidence)return false;
 if(o.type!=PacketType::Auth)for(uint8_t x:o.token)if(x)return false;
 if((o.type==PacketType::Auth||o.type==PacketType::Heartbeat)&&o.auxiliary)return false;
 p=o;return true;
}
void encodePacket(const Packet&p,uint8_t*b){std::memset(b,0,64);std::memcpy(b,"RSW1",4);b[4]=uint8_t(p.type);b[5]=uint8_t(p.source);b[6]=p.flags;b[7]=p.hazards;write32(b+8,p.session);write32(b+12,p.sequence);write64(b+16,p.phoneMs);write32(b+24,p.auxiliary);b[28]=uint8_t(p.zone);b[29]=p.confidence;std::memcpy(b+32,p.token.data(),32);}
void PhoneGate::open(uint32_t session,Ms){*this=PhoneGate{};session_=session;connected_=session!=0;}
void PhoneGate::close(){connected_=heartbeat_=synced_=pendingSync_=false;front_={};rear_={};}
void PhoneGate::requestSync(uint32_t id,Ms now){syncId_=id;syncSent_=now;pendingSync_=true;}
bool PhoneGate::online(Ms now)const{return connected_&&heartbeat_&&now>=heartbeatAt_&&now-heartbeatAt_<=3000;}
bool PhoneGate::clockValid(Ms now)const{return synced_&&now>=syncAt_&&now-syncAt_<=30000;}
Reject PhoneGate::accept(const Packet&p,Ms now){
 if(!connected_||p.session!=session_)return reject(Reject::Session);
 if(!p.sequence||p.sequence<=sequence_)return reject(Reject::Sequence);
 sequence_=p.sequence;
 if(p.phoneMs>(1ULL<<52)||now>(1ULL<<52))return reject(Reject::Timestamp);
 if(p.type==PacketType::Heartbeat){heartbeat_=true;heartbeatAt_=now;return Reject::None;}
 if(p.type==PacketType::Sync){
  if(!pendingSync_||p.auxiliary!=syncId_||now<syncSent_||now-syncSent_>400)return reject(Reject::Clock);
  pendingSync_=false;synced_=true;syncAt_=now;Ms rtt=now-syncSent_;offset_=int64_t(syncSent_+rtt/2)-int64_t(p.phoneMs);uncertainty_=(rtt+1)/2+1;return Reject::None;
 }
 if(p.type!=PacketType::Observation||(p.source!=Source::Front&&p.source!=Source::Rear))return reject(Reject::Schema);
 if(!online(now))return reject(Reject::Offline);
 if(!clockValid(now))return reject(Reject::Clock);
 auto&s=p.source==Source::Rear?rear_:front_;
 if(s.hadFrame&&p.phoneMs<=s.lastFrame)return reject(Reject::Timestamp);
 s.hadFrame=true;s.lastFrame=p.phoneMs;
 int64_t mapped=int64_t(p.phoneMs)+offset_;Ms error=uncertainty_+p.auxiliary+(now-syncAt_+4999)/5000;
 if(mapped<0||mapped>int64_t(now+error)){s={};return reject(Reject::Future);}
 int64_t age=int64_t(now)-mapped;
 if(age+int64_t(error)>500){s.value={};s.potholes=s.wet=s.vehicles=0;return reject(Reject::Stale);}
 Ms expiry=Ms(std::max<int64_t>(0,mapped+500-int64_t(error)));
 if((p.flags&0x0b)!=0x0b||p.confidence<60||expiry<=now){s.value={};s.potholes=s.wet=s.vehicles=0;return Reject::None;}
 auto persist=[&](bool present,unsigned&count,std::array<Ms,3>&times,unsigned required,Ms window){
  if(!present){count=0;return false;}
  if(count==required){for(unsigned i=1;i<count;++i)times[i-1]=times[i];--count;}
  times[count++]=p.phoneMs;
  while(count>1&&p.phoneMs-times[0]>window){for(unsigned i=1;i<count;++i)times[i-1]=times[i];--count;}
  return count==required;
 };
 Vision v;v.valid=true;v.expiresMs=expiry;v.geometryValid=p.flags&4;v.zone=p.zone;
 v.pothole=persist(p.hazards&1,s.potholes,s.potholeTimes,2,500);v.wet=persist(p.hazards&2,s.wet,s.wetTimes,3,1000);v.vehicle=persist(p.hazards&4,s.vehicles,s.vehicleTimes,2,500);s.value=v;return Reject::None;
}
Vision PhoneGate::view(Source source,Ms now)const{if(!online(now)||!clockValid(now))return {};Vision v=source==Source::Front?front_.value:rear_.value;if(now>=v.expiresMs)return {};return v;}
}
