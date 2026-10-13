#ifdef RS_WOKWI
#include <Arduino.h>
#include <esp_timer.h>
#include "core.h"
#include <cstring>
using namespace rs;
namespace {
Core core;
constexpr int IMPACT=4,CANCEL=21,SOS=14,BUZZER_LED=39,MOTOR_LED=40,STATUS_LED=41;
bool imuOn=true,impactWas=false,serialCancel=false,serialSos=false;
unsigned impactFrames=0;float injectedG=0;
Presence presence=Presence::Unknown;
char input[80];unsigned used=0;bool overflow=false;
Ms nextSample=0,lastStatus=0,visionUntil=0;
Vision syntheticVision{};bool rear=false;
Ms nowMs(){return esp_timer_get_time()/1000;}
void status(Ms now){Serial.printf("STATE=%s ALERT=%s SEND=%s remaining=%llu ms\n",stateName(core.state()),warningName(core.output(now).warning),sendName(core.sendState()),(unsigned long long)core.countdownRemaining(now));}
void command(const char* s,Ms now){
 if(!strcmp(s,"impact")){impactFrames=1;injectedG=7;}
 else if(!strcmp(s,"candidate")){impactFrames=2;injectedG=3.2f;}
 else if(!strcmp(s,"imu off"))imuOn=false;
 else if(!strcmp(s,"imu on"))imuOn=true;
 else if(!strcmp(s,"cancel down"))serialCancel=true;
 else if(!strcmp(s,"cancel up"))serialCancel=false;
 else if(!strcmp(s,"sos down"))serialSos=true;
 else if(!strcmp(s,"sos up"))serialSos=false;
 else if(!strcmp(s,"head absent"))presence=Presence::Absent;
 else if(!strcmp(s,"head present"))presence=Presence::Present;
 else if(!strcmp(s,"head unknown"))presence=Presence::Unknown;
 else if(!strcmp(s,"ack"))core.acknowledge(now);
 else if(!strcmp(s,"close")){if(!core.closeIncident(now))Serial.println("Close needs an active incident with SMS submitted/disabled.");}
 else if(!strcmp(s,"gps"))core.gps({true,6.9,79.8,now},now);
 else if(!strcmp(s,"clear")){visionUntil=0;core.vision({},false);core.vision({},true);}
 else if(!strcmp(s,"pothole")||!strcmp(s,"wet")||!strcmp(s,"rear")){
  syntheticVision={};syntheticVision.valid=true;syntheticVision.pothole=!strcmp(s,"pothole");syntheticVision.wet=!strcmp(s,"wet");syntheticVision.vehicle=!strcmp(s,"rear");rear=syntheticVision.vehicle;visionUntil=now+5000;
  core.vision({},false);core.vision({},true);Serial.println("Synthetic advisory stream for 5 seconds; this bypasses phone/model validation.");
 }
 else if(strcmp(s,"status")&&strcmp(s,"help"))Serial.println("Unknown command. Type help.");
 if(!strcmp(s,"help"))Serial.println("impact | candidate | imu on/off | cancel down/up | sos down/up | head absent/present/unknown | gps | pothole | wet | rear | clear | ack | close | status");
 status(now);
}
}
void setup(){
 Serial.begin(115200);
 for(int p:{IMPACT,CANCEL,SOS})pinMode(p,INPUT_PULLUP);
 for(int p:{BUZZER_LED,MOTOR_LED,STATUS_LED}){digitalWrite(p,LOW);pinMode(p,OUTPUT);}
 const Ms now=nowMs();core.begin(now,1,1);core.tick(now,digitalRead(CANCEL)==LOW,digitalRead(SOS)==LOW);nextSample=now;
 Serial.println("RIDESHIELD WOKWI CORE HARNESS - synthetic IMU, no real SMS. Type help.");
}
void loop(){
 const Ms now=nowMs();
 for(unsigned n=0;n<32&&Serial.available();++n){char c=Serial.read();if(c=='\r')continue;if(c=='\n'){if(!overflow){input[used]=0;command(input,now);}else Serial.println("Command too long");used=0;overflow=false;}else if(used+1<sizeof input&&!overflow)input[used++]=c;else overflow=true;}
 if(now<nextSample){delay(1);return;}nextSample=now+5;
 const bool impact=digitalRead(IMPACT)==LOW;if(impact&&!impactWas){impactFrames=1;injectedG=7;}impactWas=impact;
 core.head(presence,now);
 core.tick(now,serialCancel||digitalRead(CANCEL)==LOW,serialSos||digitalRead(SOS)==LOW);
 if(imuOn){Motion m;m.ms=now;if(impactFrames){m.ax=injectedG;--impactFrames;}core.motion(m,now);}else core.imuUnavailable(now);
 if(now<visionUntil){syntheticVision.expiresMs=now+500;core.vision(syntheticVision,rear);}
 const Output o=core.output(now);digitalWrite(BUZZER_LED,o.buzzer);digitalWrite(MOTOR_LED,o.left);digitalWrite(STATUS_LED,o.led);
 SmsJob job;if(core.takeSms(now,job)){Serial.printf("SMS PREVIEW ONLY: %s\n",job.text);core.smsResult(job.id,job.attempt,SendResult::Disabled,now);}
 Event event;while(core.popEvent(event))Serial.printf("EVENT %llu %s\n",(unsigned long long)event.ms,kindName(event.kind));
 if(now-lastStatus>=1000){status(now);lastStatus=now;}
 delay(1);
}
#endif
