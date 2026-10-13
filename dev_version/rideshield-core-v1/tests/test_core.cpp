#include "core.h"
#include "phone.h"
#include "codec.h"
#include "blackbox.h"
#include "sms_modem.h"
#include <vector>
#include <functional>
#include <stdexcept>
#include <iostream>
#include <cstring>
#include <limits>
#include <random>
using namespace rs;
using Test=std::pair<const char*,std::function<void()>>;
std::vector<Test>& tests(){static std::vector<Test> t;return t;}
#define TEST(name) void name();static bool reg_##name=[](){tests().push_back({#name,name});return true;}();void name()
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string(#x)+" at line "+std::to_string(__LINE__));}while(0)
struct Rig{
 Core c;Ms t=1000;bool cancel=false,sos=false;
 explicit Rig(Config cfg={}):c(cfg){c.begin(t,1,2);c.tick(t,false,false);Motion m;m.ms=t;c.motion(m,t);}
 void step(Ms duration,float ax=0,float w=0,float az=1){for(Ms i=0;i<duration;i+=5){t+=5;c.tick(t,cancel,sos);Motion m;m.ms=t;m.ax=ax;m.az=az;m.gx=w;c.motion(m,t);}}
 void impact(){step(5,7);CHECK(c.state()==State::Countdown);}
 void manual(){sos=true;step(3040);CHECK(c.state()==State::Incident);sos=false;step(40);}
};
TEST(normal_motion_and_gaps){Rig r;r.step(100);CHECK(r.c.state()==State::Monitor);r.t+=30;Motion m;m.ms=r.t;CHECK(r.c.motion(m,r.t));CHECK(r.c.sampleGaps()==1);}
TEST(strong_impact_countdown_without_gps){Rig r;r.impact();CHECK(r.c.output(r.t).buzzer);r.step(15000);SmsJob j;CHECK(r.c.takeSms(r.t,j));CHECK(strstr(j.text,"LOCATION UNAVAILABLE"));}
TEST(weak_candidate_needs_two_consecutive_samples){Rig r;r.step(5,3.2);CHECK(r.c.state()==State::Monitor);r.step(5,3.2);CHECK(r.c.state()==State::Verify);}
TEST(gap_breaks_candidate_streak){Rig r;r.step(5,3.2);r.t+=25;r.step(5,3.2);CHECK(r.c.state()==State::Monitor);}
TEST(unsupported_candidate_rejected){Rig r;r.step(10,3.2);r.step(3000,0,20,1.4);CHECK(r.c.state()==State::Monitor);}
TEST(rotation_support_enters_countdown){Rig r;r.step(10,3.2);r.step(5,0,250);CHECK(r.c.state()==State::Countdown);}
TEST(stillness_requires_continuous_samples){Rig r;r.step(10,3.2);r.step(800);CHECK(r.c.state()==State::Verify);r.t+=25;r.step(800);CHECK(r.c.state()==State::Verify);r.step(205);CHECK(r.c.state()==State::Countdown);}
TEST(clipping_is_per_axis){Rig r;Motion m;m.ms=++r.t;m.ax=12;m.ay=12;CHECK(r.c.motion(m,r.t));CHECK(!r.c.incident().clipped);Rig s;s.step(5,15.9);CHECK(s.c.incident().clipped);}
TEST(nonfinite_and_stale_samples_do_not_trigger){Rig r;Motion m;m.ms=++r.t;m.ax=std::numeric_limits<float>::quiet_NaN();CHECK(!r.c.motion(m,r.t));m.ax=1e30;CHECK(!r.c.motion(m,r.t));m.ax=7;m.ms=r.t-50;CHECK(!r.c.motion(m,r.t));CHECK(r.c.state()!=State::Countdown);}
TEST(unworn_drop_is_recorded_without_countdown){Rig r;r.c.head(Presence::Absent,r.t);r.step(5,7);CHECK(r.c.state()==State::Monitor);Event e;bool handling=false;while(r.c.popEvent(e))handling|=e.kind==Kind::Handling;CHECK(handling);}
TEST(recent_presence_and_unknown_wear_do_not_suppress){Rig r;r.c.head(Presence::Present,r.t);r.c.head(Presence::Absent,r.t+1);r.impact();Rig s;s.impact();}
TEST(stale_absence_does_not_suppress){Rig r;r.c.head(Presence::Absent,r.t);r.step(1600);r.impact();}
TEST(removing_helmet_does_not_cancel){Rig r;r.impact();r.c.head(Presence::Absent,r.t);r.step(1000);CHECK(r.c.state()==State::Countdown);}
TEST(cancel_requires_new_deliberate_hold){Rig r;r.impact();r.cancel=true;r.step(500);CHECK(r.c.state()==State::Countdown);r.step(540);CHECK(r.c.state()==State::Monitor);}
TEST(cancel_held_at_entry_requires_release){Rig r;r.cancel=true;r.step(100);r.impact();r.step(1200);CHECK(r.c.state()==State::Countdown);r.cancel=false;r.step(40);r.cancel=true;r.step(1040);CHECK(r.c.state()==State::Monitor);}
TEST(cancel_bounce_is_not_a_hold){Rig r;r.impact();for(int i=0;i<100;++i){r.cancel=!r.cancel;r.step(10);}CHECK(r.c.state()==State::Countdown);}
TEST(sos_held_at_boot_is_disarmed){Core c;c.begin(0,1,2);for(Ms t=0;t<4000;t+=5)c.tick(t,false,true);CHECK(c.state()!=State::Incident);c.tick(4000,false,false);c.tick(4040,false,false);c.tick(4050,false,true);c.tick(4090,false,true);c.tick(7100,false,true);CHECK(c.state()==State::Incident);}
TEST(manual_sos_bypasses_wear_and_missing_imu){Rig r;r.c.head(Presence::Absent,r.t);r.c.imuUnavailable(r.t);r.sos=true;for(int i=0;i<610;++i){r.t+=5;r.c.tick(r.t,false,true);}CHECK(r.c.state()==State::Incident);CHECK(r.c.incident().manual);}
TEST(simultaneous_sos_wins_over_cancel){Config cfg;cfg.cancelHoldMs=cfg.sosHoldMs;Rig r(cfg);r.impact();r.cancel=r.sos=true;r.step(3040);CHECK(r.c.state()==State::Incident);CHECK(r.c.incident().manual);}
TEST(imu_loss_preserves_countdown_and_escalates_verify){Rig r;r.step(10,3.2);r.c.imuUnavailable(r.t);CHECK(r.c.state()==State::Countdown);CHECK(r.c.incident().uncertain);r.c.imuUnavailable(r.t+5);CHECK(r.c.state()==State::Countdown);}
TEST(buttons_and_incidents_work_beyond_32_bit_millis){Rig r;r.t=(1ULL<<32)+1000;r.c.begin(r.t,1,3);r.c.tick(r.t,false,false);r.manual();CHECK(r.c.incident().startedMs>(1ULL<<32));}
TEST(sms_retries_keep_id_and_submitted_stops_retries){Rig r;r.manual();SmsJob a,b;CHECK(r.c.takeSms(r.t,a));CHECK(!r.c.smsResult(a.id,a.attempt+1,SendResult::Submitted,r.t));CHECK(r.c.smsResult(a.id,a.attempt,SendResult::Failed,r.t));CHECK(!r.c.takeSms(r.t+14000,b));CHECK(r.c.takeSms(r.t+16000,b));CHECK(a.id==b.id);CHECK(b.attempt==2);CHECK(r.c.smsResult(b.id,b.attempt,SendResult::Submitted,r.t+16000));CHECK(!r.c.takeSms(r.t+999999,b));}
TEST(acknowledgement_does_not_stop_retry){Rig r;r.manual();r.c.acknowledge(r.t);CHECK(!r.c.closeIncident(r.t));SmsJob j;CHECK(r.c.takeSms(r.t,j));CHECK(r.c.output(r.t).warning!=Warning::Emergency);}
TEST(gps_fresh_old_and_invalid_labels){Rig r;r.c.gps({true,6.9,79.8,r.t},r.t);r.manual();SmsJob j;CHECK(r.c.takeSms(r.t,j));CHECK(strstr(j.text,"LOC 6.90000,79.80000"));r.c.smsResult(j.id,j.attempt,SendResult::Failed,r.t);r.c.gps({true,999,999,r.t},r.t);CHECK(r.c.takeSms(r.t+16000,j));CHECK(strstr(j.text,"OLD LOC"));}
TEST(storage_queue_overflow_cannot_suppress_emergency){Rig r;for(int i=0;i<100;++i){r.c.imuUnavailable(r.t);r.step(5);}CHECK(r.c.droppedEvents()>0);r.manual();SmsJob j;CHECK(r.c.takeSms(r.t,j));}
TEST(journal_roundtrip_crc_and_recovery){Rig r;r.manual();SmsJob j;CHECK(r.c.takeSms(r.t,j));uint8_t b[CHECKPOINT_SIZE];encodeCheckpoint(r.c.checkpoint(),b);Checkpoint cp;CHECK(decodeCheckpoint(b,sizeof b,cp));Core c;c.begin(9999,1,9);c.restore(cp,9999);CHECK(c.sendState()==SendState::Unknown);CHECK(c.incident().id==j.id);b[10]^=1;CHECK(!decodeCheckpoint(b,sizeof b,cp));}
TEST(recover_submitted_does_not_resend_and_countdown_restarts){Rig r;r.manual();SmsJob j;CHECK(r.c.takeSms(r.t,j));r.c.smsResult(j.id,j.attempt,SendResult::Submitted,r.t);Core c;c.begin(0,1,4);c.restore(r.c.checkpoint(),0);CHECK(!c.takeSms(100000,j));Rig s;s.impact();c.restore(s.c.checkpoint(),100);CHECK(c.countdownRemaining(100)==15000);}
struct PhoneRig{
 PhoneGate gate;uint32_t seq=0;
 PhoneRig(){gate.open(42,1000);gate.requestSync(7,1000);Packet p;p.type=PacketType::Sync;p.session=42;p.sequence=++seq;p.phoneMs=2000;p.auxiliary=7;CHECK(gate.accept(p,1020)==Reject::None);heartbeat(1020);}
 void heartbeat(Ms t){Packet p;p.session=42;p.sequence=++seq;CHECK(gate.accept(p,t)==Reject::None);}
 Packet obs(Ms capture,uint8_t bits=1,Source source=Source::Front){Packet p;p.type=PacketType::Observation;p.source=source;p.session=42;p.sequence=++seq;p.phoneMs=capture;p.flags=11;p.confidence=90;p.hazards=bits;return p;}
};
TEST(phone_needs_distinct_fresh_frames_and_expires){PhoneRig r;CHECK(r.gate.accept(r.obs(2010),1020)==Reject::None);CHECK(!r.gate.view(Source::Front,1020).pothole);CHECK(r.gate.accept(r.obs(2110),1120)==Reject::None);CHECK(r.gate.view(Source::Front,1120).pothole);CHECK(!r.gate.view(Source::Front,1700).valid);}
TEST(phone_rejects_stale_replay_future_and_wrong_session){PhoneRig r;auto p=r.obs(1000);CHECK(r.gate.accept(p,1020)==Reject::Stale);CHECK(r.gate.accept(p,1020)==Reject::Sequence);p=r.obs(9000);CHECK(r.gate.accept(p,1020)==Reject::Future);p=r.obs(2010);p.session=43;CHECK(r.gate.accept(p,1020)==Reject::Session);}
TEST(phone_uncertainty_and_missing_clock_disable_warnings){PhoneRig r;auto p=r.obs(2010);p.auxiliary=1000;CHECK(r.gate.accept(p,1020)==Reject::Stale);PhoneGate g;g.open(42,1000);p.type=PacketType::Heartbeat;CHECK(g.accept(p,1020)==Reject::None);p=r.obs(2110);CHECK(g.accept(p,1120)==Reject::Clock);}
TEST(phone_heartbeat_does_not_refresh_camera_results){PhoneRig r;r.gate.accept(r.obs(2010),1020);r.gate.accept(r.obs(2110),1120);r.heartbeat(2000);CHECK(r.gate.online(2000));CHECK(!r.gate.view(Source::Front,2000).valid);}
TEST(phone_zero_confidence_and_duplicate_capture_do_not_persist){PhoneRig r;auto p=r.obs(2010);p.confidence=0;CHECK(r.gate.accept(p,1020)==Reject::None);CHECK(!r.gate.view(Source::Front,1020).valid);p=r.obs(2010);CHECK(r.gate.accept(p,1020)==Reject::Timestamp);}
TEST(vision_cannot_create_incident_and_direction_is_disabled_by_default){Rig r;Vision v;v.valid=v.vehicle=v.geometryValid=true;v.zone=Zone::Left;v.expiresMs=5000;r.c.vision(v,true);auto o=r.c.output(r.t);CHECK(o.warning==Warning::Rear);CHECK(o.left&&o.right);CHECK(r.c.state()==State::Monitor);}
TEST(advisory_pulses_immediately_when_a_new_detection_arrives){Rig r;r.step(700);Vision v;v.valid=v.pothole=true;v.expiresMs=r.t+400;r.c.vision(v,false);CHECK(r.c.output(r.t).left);CHECK(!r.c.output(r.t).buzzer);}
TEST(sustained_phone_frames_remain_confirmed_across_windows){PhoneRig r;for(unsigned i=0;i<20;++i){Ms t=1020+100*i;CHECK(r.gate.accept(r.obs(2010+100*i,3),t)==Reject::None);auto v=r.gate.view(Source::Front,t);if(i>=2)CHECK(v.pothole&&v.wet);}}
TEST(clock_and_heartbeat_expire_independently){PhoneRig r;CHECK(!r.gate.online(5000));CHECK(r.gate.clockValid(5000));r.heartbeat(40000);CHECK(r.gate.online(40000));CHECK(!r.gate.clockValid(40000));}
TEST(possible_duplicate_label_survives_intermediate_failure){Rig r;r.manual();SmsJob j;CHECK(r.c.takeSms(r.t,j));r.c.smsResult(j.id,j.attempt,SendResult::Unknown,r.t);CHECK(r.c.takeSms(r.t+16000,j));r.c.smsResult(j.id,j.attempt,SendResult::Failed,r.t+16000);CHECK(r.c.takeSms(r.t+48000,j));CHECK(strstr(j.text,"RETRY?"));}
TEST(binary_packet_schema_and_roundtrip){PhoneRig r;auto p=r.obs(2010);uint8_t b[64];encodePacket(p,b);Packet q;CHECK(decodePacket(b,64,q));CHECK(q.sequence==p.sequence);b[30]=1;CHECK(!decodePacket(b,64,q));b[30]=0;b[7]=4;CHECK(!decodePacket(b,64,q));CHECK(!decodePacket(b,63,q));}
TEST(binary_protocol_matches_python_golden_vector){PhoneRig r;uint8_t b[64];encodePacket(r.obs(2010),b);const char* hex="5253573104010b012a00000003000000da0700000000000000000000005a0000";for(int i=0;i<32;++i){char pair[3]={hex[i*2],hex[i*2+1],0};CHECK(b[i]==std::stoul(pair,nullptr,16));}for(int i=32;i<64;++i)CHECK(b[i]==0);}
TEST(crc_known_vector_and_record_corruption){CHECK(crc32(reinterpret_cast<const uint8_t*>("123456789"),9)==0xcbf43926);Event e;uint8_t b[48];encodeEvent(e,b);CHECK(validRecord(b,48));b[20]^=1;CHECK(!validRecord(b,48));}
TEST(blackbox_retains_pretrigger_and_stops_overwriting_export){MotionRecord pre[3],event[8];BlackBox b(pre,3,event,8,20);Motion m;for(Ms t=0;t<=20;t+=5){m.ms=t;b.push(m);}CHECK(b.trigger({1,2,3},20));for(Ms t=25;t<=40;t+=5){m.ms=t;b.push(m);}b.tick(40);const MotionRecord* data;size_t n;Id id;bool truncated;CHECK(b.acquire(data,n,id,truncated));CHECK(n==7);CHECK(data[0].motion.ms==10);CHECK(data[6].motion.ms==40);m.ms=45;b.push(m);CHECK(data[6].motion.ms==40);CHECK(!b.trigger({1,2,4},45));CHECK(b.missedCaptures()==1);b.release();CHECK(b.status()==BlackBox::Status::Idle);}
TEST(blackbox_flags_limited_capacity){MotionRecord pre[2],event[2];BlackBox b(pre,2,event,2,20);Motion m;b.push(m);CHECK(b.trigger({1,2,3},0));m.ms=5;b.push(m);m.ms=10;b.push(m);b.tick(20);const MotionRecord* d;size_t n;Id id;bool truncated;CHECK(b.acquire(d,n,id,truncated));CHECK(truncated);}
std::string tx(SmsModem&m){uint8_t b[192];size_t n=m.takeTx(b,sizeof b);return std::string(reinterpret_cast<char*>(b),n);}
void feed(SmsModem&m,const char* s,Ms t){for(;*s;++s)m.feed(*s,t);}
void modemToPrompt(SmsModem&m){SmsJob j;strcpy(j.text,"TEST RideShield");CHECK(m.start(j,"+94712345678",0));CHECK(tx(m)=="AT\r");feed(m,"\r\nOK\r\n",10);CHECK(tx(m)=="ATE0\r");feed(m,"OK\r\n",20);CHECK(tx(m)=="AT+CPIN?\r");feed(m,"+CPIN: READY\r\nOK\r\n",30);CHECK(tx(m)=="AT+CEREG?\r");feed(m,"+CEREG: 0,1\r\nOK\r\n",40);CHECK(tx(m)=="AT+CMGF=1\r");feed(m,"OK\r\n",50);CHECK(tx(m)=="AT+CSCS=\"GSM\"\r");feed(m,"OK\r\n",60);CHECK(tx(m)=="AT+CMGS=\"+94712345678\"\r");}
TEST(modem_requires_cmgs_and_handles_submission){SmsModem m;modemToPrompt(m);feed(m,">",70);CHECK(tx(m).back()==26);feed(m,"OK\r\n",80);SendResult r;CHECK(!m.takeResult(r));feed(m,"+CMGS: 7\r\nOK\r\n",90);CHECK(m.takeResult(r));CHECK(r==SendResult::Submitted);}
TEST(modem_timeout_after_body_is_unknown){SmsModem m;modemToPrompt(m);feed(m,">",70);tx(m);m.tick(60070);SendResult r;CHECK(m.takeResult(r));CHECK(r==SendResult::Unknown);}
TEST(modem_cmgs_without_final_ok_is_submitted){SmsModem m;modemToPrompt(m);feed(m,">",70);tx(m);feed(m,"+CMGS: 7\r\n",80);m.tick(60070);SendResult r;CHECK(m.takeResult(r));CHECK(r==SendResult::Submitted);}
TEST(modem_prompt_timeout_is_failure_not_submission){SmsModem m;modemToPrompt(m);m.tick(10060);SendResult r;CHECK(m.takeResult(r));CHECK(r==SendResult::Failed);}
TEST(modem_missing_sim_or_registration_does_not_send_body){SmsModem m;SmsJob j;strcpy(j.text,"TEST");CHECK(m.start(j,"+94712345678",0));tx(m);feed(m,"OK\r\n",10);tx(m);feed(m,"OK\r\n",20);tx(m);feed(m,"+CPIN: SIM PIN\r\nOK\r\n",30);CHECK(tx(m).empty());SendResult r;CHECK(m.takeResult(r));CHECK(r==SendResult::Failed);}
TEST(modem_rejects_invalid_number_and_at_injection){SmsModem m;SmsJob j;strcpy(j.text,"TEST");CHECK(!m.start(j,"+94712345678\rAT",0));strcpy(j.text,"TEST\x1a");CHECK(!m.start(j,"+94712345678",0));}
TEST(modem_malformed_cmgs_does_not_claim_submission){SmsModem m;modemToPrompt(m);feed(m,">",70);tx(m);feed(m,"+CMGS: 7junk\r\nOK\r\n",80);m.tick(60070);SendResult r;CHECK(m.takeResult(r));CHECK(r==SendResult::Unknown);}
TEST(fuzz_binary_inputs_and_modem_lines){std::mt19937 gen(20261012);SmsModem m;modemToPrompt(m);for(unsigned i=0;i<100000;++i){uint8_t b[96];for(auto& x:b)x=uint8_t(gen());size_t n=gen()%97;Packet p;Checkpoint c;decodePacket(b,n,p);decodeCheckpoint(b,n,c);validRecord(b,n);m.feed(char(gen()),70+i/100);}m.tick(100000);}
TEST(blackbox_fallback_is_explicitly_marked){MotionRecord h[2],e[8];BlackBox b(h,2,e,8,10,true);Motion m;b.push(m);CHECK(b.trigger({1,2,3},0));b.tick(10);const MotionRecord* data;size_t n;Id id;bool truncated;CHECK(b.acquire(data,n,id,truncated));CHECK(truncated);}
int main(){unsigned failed=0;for(const auto& t:tests()){try{t.second();std::cout<<"PASS "<<t.first<<'\n';}catch(const std::exception&e){++failed;std::cerr<<"FAIL "<<t.first<<": "<<e.what()<<'\n';}}std::cout<<tests().size()-failed<<"/"<<tests().size()<<" passed\n";return failed?1:0;}
