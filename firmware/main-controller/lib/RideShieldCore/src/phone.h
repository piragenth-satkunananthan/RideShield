#pragma once
#include "core.h"
namespace rs {
enum class PacketType:uint8_t{Auth=1,Sync,Heartbeat,Observation};
enum class Source:uint8_t{None,Front,Rear};
enum class Reject:uint8_t{None,Session,Sequence,Clock,Offline,Future,Stale,Timestamp,Schema};
struct Packet{
 PacketType type=PacketType::Heartbeat;Source source=Source::None;uint8_t flags=0,hazards=0;
 uint32_t session=0,sequence=0;Ms phoneMs=0;uint32_t auxiliary=0;Zone zone=Zone::Unknown;uint8_t confidence=0;std::array<uint8_t,32>token{};
};
bool decodePacket(const uint8_t*,size_t,Packet&);void encodePacket(const Packet&,uint8_t*);
class PhoneGate{
public:
 void open(uint32_t,Ms);void close();void requestSync(uint32_t,Ms);Reject accept(const Packet&,Ms);
 Vision view(Source,Ms)const;bool online(Ms)const;bool clockValid(Ms)const;uint32_t rejected()const{return rejected_;}
private:
 struct Stream{Vision value{};Ms lastFrame=0;std::array<Ms,3>potholeTimes{},wetTimes{},vehicleTimes{};unsigned potholes=0,wet=0,vehicles=0;bool hadFrame=false;}front_,rear_;
 uint32_t session_=0,sequence_=0,syncId_=0,rejected_=0;bool connected_=false,heartbeat_=false,synced_=false,pendingSync_=false;
 Ms heartbeatAt_=0,syncSent_=0,syncAt_=0;int64_t offset_=0;Ms uncertainty_=0;
 Reject reject(Reject r){++rejected_;return r;}
};
}
