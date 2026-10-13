#include "core.h"
#include "phone.h"
#include <cassert>
#include <iostream>
using namespace rs;
int main(){
 Core c;Ms now=1000;c.begin(now,1,2);c.tick(now,false,false);
 auto step=[&](Ms duration,bool cancel=false,bool sos=false,float ax=0,bool imu=true){for(Ms i=0;i<duration;i+=5){now+=5;c.tick(now,cancel,sos);if(imu){Motion m;m.ms=now;m.ax=ax;c.motion(m,now);}}};
 step(5);step(5,false,false,7);assert(c.state()==State::Countdown);step(1040,true);assert(c.state()==State::Monitor);std::cout<<"PASS impact -> deliberate cancel -> monitor\n";
 step(40);step(5,false,false,7);step(15000);SmsJob j;assert(c.takeSms(now,j));std::cout<<"PASS impact -> countdown -> SMS job: "<<j.text<<'\n';c.smsResult(j.id,j.attempt,SendResult::Disabled,now);assert(c.closeIncident(now));
 step(3040,false,true,0,false);assert(c.state()==State::Incident&&c.incident().manual);std::cout<<"PASS SOS with missing IMU\n";
 PhoneGate gate;gate.open(42,1000);gate.requestSync(1,1000);Packet p;p.session=42;p.sequence=1;p.type=PacketType::Sync;p.phoneMs=2000;p.auxiliary=1;assert(gate.accept(p,1020)==Reject::None);
 p.sequence++;p.type=PacketType::Heartbeat;p.auxiliary=0;assert(gate.accept(p,1020)==Reject::None);
 p.type=PacketType::Observation;p.source=Source::Rear;p.flags=11;p.hazards=4;p.confidence=90;p.phoneMs=2010;p.sequence++;assert(gate.accept(p,1020)==Reject::None);p.phoneMs=2110;p.sequence++;assert(gate.accept(p,1120)==Reject::None);assert(gate.view(Source::Rear,1120).vehicle);assert(!gate.view(Source::Rear,1700).valid);std::cout<<"PASS rear observation confirmed then expires\nNo hardware used; no SMS sent.\n";
}
