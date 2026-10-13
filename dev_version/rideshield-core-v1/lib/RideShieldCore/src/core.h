#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace rs {
using Ms=uint64_t;
enum class State:uint8_t{Monitor,Degraded,Verify,Countdown,Incident};
enum class Presence:uint8_t{Unknown,Absent,Present};
enum class SendState:uint8_t{None,Queued,Attempting,Submitted,Failed,Unknown,Disabled};
enum class SendResult:uint8_t{Submitted,Failed,Unknown,Disabled};
enum class Warning:uint8_t{None,Rear,WetRoad,Pothole,RoadCombined,System,Countdown,Emergency};
enum class Zone:uint8_t{Unknown,Left,Centre,Right};
enum class Kind:uint16_t{Boot=1,Candidate,Handling,Rejected,Countdown,Cancelled,Emergency,ImuLost,ImuRestored,ImuGap,SmsAttempt,SmsSubmitted,SmsFailed,SmsUnknown,SmsDisabled,Acknowledged,Closed,Recovered,AdditionalImpact};
struct Config{
 float candidateG=3,directG=6,supportDps=200;
 Ms verifyMs=3000,countdownMs=15000,cancelHoldMs=1000,sosHoldMs=3000;
 Ms buttonDebounceMs=30,imuGapMs=20,imuFaultMs=100;
 Ms recentHeadMs=10000,headStaleMs=1500,gpsFreshMs=10000;
 bool allowDirectional=false;
};
struct Id{uint32_t device=0,boot=0,sequence=0;};
bool operator==(const Id&,const Id&);
struct Motion{Ms ms=0;float ax=0,ay=0,az=1,gx=0,gy=0,gz=0;bool valid=true;};
struct GpsFix{bool valid=false;double lat=0,lon=0;Ms ms=0;};
struct Vision{bool valid=false,pothole=false,wet=false,vehicle=false,geometryValid=false;Zone zone=Zone::Unknown;Ms expiresMs=0;};
struct Incident{
 Id id{};Ms startedMs=0;
 bool manual=false,uncertain=false,clipped=false,recovered=false,possibleDuplicate=false,localAcknowledged=false;
 float peakG=0,peakDps=0;uint32_t sampleGaps=0,clippedSamples=0;
};
struct Event{Ms ms=0;Id id{};Kind kind=Kind::Boot;float value=0;uint32_t detail=0;};
struct SmsJob{Id id{};uint32_t attempt=0;char text[161]{};};
struct Checkpoint{State state=State::Monitor;SendState send=SendState::None;Incident incident{};};
struct Output{Warning warning=Warning::None;bool left=false,right=false,buzzer=false,led=false;};
// Single owner: only the controller task may call Core methods.
class Core{
public:
 explicit Core(Config cfg={});
 void begin(Ms,uint32_t,uint32_t);
 void tick(Ms,bool cancelPressed,bool sosPressed);
 bool motion(const Motion&,Ms);
 void imuUnavailable(Ms);
 void head(Presence,Ms);
 void gps(const GpsFix&,Ms);
 void vision(const Vision&,bool rear);
 void systemFault(bool f){systemFault_=f;}
 bool takeSms(Ms,SmsJob&);
 bool smsResult(Id,uint32_t,SendResult,Ms);
 void acknowledge(Ms);
 bool closeIncident(Ms);
 void restore(const Checkpoint&,Ms);
 Checkpoint checkpoint()const;
 Output output(Ms)const;
 bool popEvent(Event&);
 State state()const{return state_;}
 SendState sendState()const{return send_;}
 const Incident& incident()const{return incident_;}
 uint32_t revision()const{return revision_;}
 uint32_t droppedEvents()const{return dropped_;}
 uint32_t sampleGaps()const{return gaps_;}
 bool imuHealthy()const{return imuHealthy_;}
 Ms countdownRemaining(Ms)const;
 const Config& config()const{return cfg_;}
private:
 struct Button{
  bool initialized=false,raw=false,stable=false,armed=false,fired=false;
  Ms changedMs=0,pressedMs=0;
  bool update(bool,Ms,Ms,Ms);void requireRelease();
 }cancel_,sos_;
 Config cfg_;State state_=State::Degraded;SendState send_=SendState::None;
 uint32_t device_=0,boot_=0,counter_=0,revision_=0;
 Incident incident_{};
 bool imuHealthy_=false,hadMotion_=false,systemFault_=false;
 Ms lastMotion_=0,bootMs_=0,countdownAt_=0,emergencyAt_=0,now_=0;
 unsigned above_=0;
 Presence head_=Presence::Unknown;bool everPresent_=false;Ms headAt_=0,lastPresent_=0;
 GpsFix fix_{};Vision front_{},rear_{};Ms frontWarningAt_=0,rearWarningAt_=0;
 uint32_t gaps_=0,attempt_=0;Ms retryAt_=0,attemptAt_=0,lastImpactEvent_=0;
 Ms stillStart_=0,stillLast_=0;uint32_t stillN_=0;double stillMean_=0,stillM2_=0;float stillMaxW_=0;
 std::array<Event,64> events_{};size_t eventHead_=0,eventCount_=0;uint32_t dropped_=0;
 void log(Kind,Ms,float=0,uint32_t=0);void mark(){++revision_;}
 bool confidentlyUnworn(Ms)const;
 void startCandidate(Ms,float,float,bool);void startCountdown(Ms);void emergency(Ms,bool);
 void resetStillness();bool stillness(Ms,float,float);void clearToMonitor();
};
const char* stateName(State);const char* sendName(SendState);const char* warningName(Warning);const char* kindName(Kind);
void formatId(const Id&,char*,size_t);bool validPhoneNumber(const char*);
}
