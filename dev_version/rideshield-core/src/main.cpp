#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsServer.h>
#include <TinyGPS++.h>
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>
#include <esp_timer.h>
#include <esp_task_wdt.h>
#include <esp_heap_caps.h>
#include <atomic>
#include <new>
#include <cmath>
#include "board_config.h"
#include "core.h"
#include "codec.h"
#include "phone.h"
#include "blackbox.h"
#include "sms_modem.h"
#include "mpu6050.h"
using namespace rs;
namespace {
Ms clockMs(){return esp_timer_get_time()/1000;}
Core core;Mpu6050 imu;BlackBox* recorder=nullptr;
uint32_t deviceId,bootId;
struct VisionPair{Vision front,rear;bool online,clock;uint32_t rejected;Ms published;};
struct Result{Id id;uint32_t attempt;SendResult outcome;};
struct Journal{uint8_t bytes[CHECKPOINT_SIZE];};
struct Capture{const MotionRecord* records;size_t count;Id id;bool truncated;};
struct Command{enum Type{Impact,Candidate,Imu,Cancel,Sos,Head,Ack,Close,Gps}type;int value=0;double lat=0,lon=0;};
struct Status{Ms now,remaining;State state;SendState send;Warning warning;bool imu,phone,clock,front,rear;uint32_t gaps,dropped,rejected,missed;};
QueueHandle_t visionQ,smsQ,resultQ,journalQ,eventQ,captureQ,captureDoneQ,commandQ,gpsQ;
std::atomic<bool> sdFault{false},journalFault{false},captureFault{false},networkFault{false},recoveryFault{false};
std::atomic<uint32_t> eventDrops{0};
portMUX_TYPE statusMux=portMUX_INITIALIZER_UNLOCKED;Status sharedStatus{};
static_assert(pdMS_TO_TICKS(5)>0,"FreeRTOS tick must support 5 ms scheduling");
static_assert(WEBSOCKETS_MAX_DATA_SIZE<=16384,"Review transport memory bounds");
void setPin(int pin,bool value){if(pin>=0)digitalWrite(pin,value?HIGH:LOW);}
void applyOutput(const Output& o){
#if RS_ENABLE_OUTPUTS
 setPin(board::BUZZER,o.buzzer);setPin(board::LEFT_MOTOR,o.left);setPin(board::RIGHT_MOTOR,o.right);setPin(board::LED,o.led);
#else
 (void)o;
#endif
}
void controllerTask(void*){
 esp_task_wdt_add(nullptr);TickType_t wake=xTaskGetTickCount();
 bool benchImu=true,cancel=false,sos=false;Presence presence=Presence::Unknown;unsigned impactFrames=0;float impactG=0;
 uint32_t journalRevision=UINT32_MAX;Id captured{};VisionPair pair{};
 for(;;){
  const Ms now=clockMs();Command cmd;
  for(unsigned i=0;i<4&&xQueueReceive(commandQ,&cmd,0)==pdTRUE;++i){
   if(cmd.type==Command::Ack)core.acknowledge(now);
   else if(cmd.type==Command::Close)core.closeIncident(now);
#if RS_BENCH
   else switch(cmd.type){
    case Command::Impact:impactFrames=1;impactG=7;break;
    case Command::Candidate:impactFrames=2;impactG=3.2f;break;
    case Command::Imu:benchImu=cmd.value;break;
    case Command::Cancel:cancel=cmd.value;break;
    case Command::Sos:sos=cmd.value;break;
    case Command::Head:presence=Presence(cmd.value);break;
    case Command::Gps:core.gps({true,cmd.lat,cmd.lon,now},now);break;
    default:break;
   }
#endif
  }
#if RS_BENCH
  core.head(presence,now);
#else
  (void)benchImu;(void)presence;(void)impactFrames;(void)impactG;
#endif
  core.tick(now,cancel||digitalRead(board::CANCEL)==LOW,sos||digitalRead(board::SOS)==LOW);
  Motion samples[4];int count=0;
#if RS_BENCH
  if(benchImu){samples[0].ms=now;if(impactFrames){samples[0].ax=impactG;--impactFrames;}count=1;}else core.imuUnavailable(now);
#else
  count=imu.poll(now,samples,4);if(count<0)core.imuUnavailable(now);
#endif
  for(int i=0;i<count;++i){
   if(core.motion(samples[i],now)){
    const auto& m=samples[i];bool clipped=std::fabs(m.ax)>=15.68f||std::fabs(m.ay)>=15.68f||std::fabs(m.az)>=15.68f||std::fabs(m.gx)>=1960||std::fabs(m.gy)>=1960||std::fabs(m.gz)>=1960;
    recorder->push(m,(RS_BENCH?1u:4u)|(clipped?2u:0u));
    const Id id=core.incident().id;if(id.sequence&&!(id==captured)){recorder->trigger(id,now);captured=id;}
   }
  }
  const Id id=core.incident().id;if(id.sequence&&!(id==captured)){recorder->trigger(id,now);captured=id;}
  if(xQueuePeek(visionQ,&pair,0)==pdTRUE){core.vision(pair.front,false);core.vision(pair.rear,true);}
  GpsFix fix;if(xQueueReceive(gpsQ,&fix,0)==pdTRUE)core.gps(fix,now);
  Result result;if(xQueueReceive(resultQ,&result,0)==pdTRUE)core.smsResult(result.id,result.attempt,result.outcome,now);
  core.systemFault(sdFault||journalFault||captureFault||networkFault||recoveryFault);
  const Output output=core.output(now);applyOutput(output);
  if(core.revision()!=journalRevision){Journal j{};encodeCheckpoint(core.checkpoint(),j.bytes);xQueueOverwrite(journalQ,&j);journalRevision=core.revision();}
  SmsJob job;if(uxQueueSpacesAvailable(smsQ)&&core.takeSms(now,job))xQueueSend(smsQ,&job,0);
  Event event;while(core.popEvent(event))if(xQueueSend(eventQ,&event,0)!=pdTRUE)++eventDrops;
  bool done;if(xQueueReceive(captureDoneQ,&done,0)==pdTRUE)recorder->release();
  recorder->tick(now);Capture capture;
  if(recorder->acquire(capture.records,capture.count,capture.id,capture.truncated)&&xQueueSend(captureQ,&capture,0)!=pdTRUE){captureFault=true;recorder->release();}
  const bool fresh=now>=pair.published&&now-pair.published<=3000;
  Status status{now,core.countdownRemaining(now),core.state(),core.sendState(),output.warning,core.imuHealthy(),fresh&&pair.online,fresh&&pair.clock,pair.front.valid&&now<pair.front.expiresMs,pair.rear.valid&&now<pair.rear.expiresMs,core.sampleGaps(),core.droppedEvents()+eventDrops.load(),pair.rejected,recorder->missedCaptures()};
  portENTER_CRITICAL(&statusMux);sharedStatus=status;portEXIT_CRITICAL(&statusMux);
  esp_task_wdt_reset();vTaskDelayUntil(&wake,pdMS_TO_TICKS(5));
 }
}
void journalTask(void*){
 Preferences prefs;bool opened=prefs.begin(board::JOURNAL,false);journalFault=!opened;Journal latest{};bool pending=false;
 for(;;){
  if(xQueueReceive(journalQ,&latest,pdMS_TO_TICKS(500))==pdTRUE)pending=true;
  if(!opened){opened=prefs.begin(board::JOURNAL,false);journalFault=!opened;}
  if(opened&&pending){const bool ok=prefs.putBytes("pending",latest.bytes,sizeof latest.bytes)==sizeof latest.bytes;journalFault=!ok;if(ok)pending=false;}
 }
}
void storageTask(void*){
 File events;
#if RS_ENABLE_SD
 SPI.begin(board::SD_SCK,board::SD_MISO,board::SD_MOSI,board::SD_CS);
 if(!SD.begin(board::SD_CS,SPI,4000000))sdFault=true;
 else {char path[48];snprintf(path,sizeof path,"/events-%08lx.rsl",(unsigned long)bootId);events=SD.open(path,FILE_APPEND);if(!events)sdFault=true;}
#endif
 Ms flushed=0;
 for(;;){
  Event e;for(unsigned n=0;n<16&&xQueueReceive(eventQ,&e,0)==pdTRUE;++n){
   char id[40];formatId(e.id,id,sizeof id);Serial.printf("EVENT %llu %s %s %.2f %lu\n",(unsigned long long)e.ms,id,kindName(e.kind),e.value,(unsigned long)e.detail);
   if(events){uint8_t bytes[EVENT_SIZE];encodeEvent(e,bytes);if(events.write(bytes,sizeof bytes)!=sizeof bytes)sdFault=true;}
  }
  Capture c;if(xQueueReceive(captureQ,&c,0)==pdTRUE){
   char id[40];formatId(c.id,id,sizeof id);Serial.printf("CAPTURE %s samples=%u truncated=%d\n",id,unsigned(c.count),c.truncated);
#if RS_ENABLE_SD
   if(!sdFault){char path[80];snprintf(path,sizeof path,"/motion-%s.rsl",id);File file=SD.open(path,FILE_WRITE);
    if(!file)sdFault=true;
    else {uint8_t b[MOTION_SIZE];for(size_t i=0;i<c.count;++i){encodeMotion(c.records[i].motion,c.records[i].flags,b);if(file.write(b,sizeof b)!=sizeof b){sdFault=true;break;}if(i%32==0)vTaskDelay(1);}
     if(!sdFault){memset(b,0,sizeof b);memcpy(b,"RSZ1",4);write32(b+4,sizeof b);write32(b+16,c.count);write32(b+20,c.truncated);write32(b+24,c.id.device);write32(b+28,c.id.boot);write32(b+32,c.id.sequence);write32(b+44,crc32(b,44));if(file.write(b,sizeof b)!=sizeof b)sdFault=true;}file.flush();file.close();}
   }
#endif
   bool done=true;xQueueOverwrite(captureDoneQ,&done);
  }
  if(events&&clockMs()-flushed>=1000){events.flush();flushed=clockMs();}vTaskDelay(pdMS_TO_TICKS(10));
 }
}
void modemTask(void*){
 HardwareSerial uart(2);
#if RS_ENABLE_LTE
 uart.begin(board::LTE_BAUD,SERIAL_8N1,board::LTE_RX,board::LTE_TX);
#endif
 SmsModem modem;SmsJob job{};bool active=false;
 for(;;){
  const Ms now=clockMs();
  if(!active&&xQueueReceive(smsQ,&job,0)==pdTRUE){
#if RS_ALLOW_SMS
   for(unsigned n=0;n<1024&&uart.available();++n)uart.read();
   active=modem.start(job,RS_SMS_NUMBER,now);if(!active){Result r{job.id,job.attempt,SendResult::Failed};xQueueOverwrite(resultQ,&r);}
#else
   Serial.printf("SMS DISABLED: %s\n",job.text);Result r{job.id,job.attempt,SendResult::Disabled};xQueueOverwrite(resultQ,&r);
#endif
  }
  if(active){uint8_t tx[192];size_t n=modem.takeTx(tx,sizeof tx);if(n)uart.write(tx,n);
   for(unsigned i=0;i<256&&uart.available();++i)modem.feed(char(uart.read()),now);
   modem.tick(now);SendResult outcome;if(modem.takeResult(outcome)){Result r{job.id,job.attempt,outcome};xQueueOverwrite(resultQ,&r);active=false;}
  }vTaskDelay(pdMS_TO_TICKS(5));
 }
}
Status statusSnapshot(){portENTER_CRITICAL(&statusMux);Status s=sharedStatus;portEXIT_CRITICAL(&statusMux);return s;}
void printStatus(){auto s=statusSnapshot();Serial.printf("STATE=%s SEND=%s ALERT=%s IMU=%d PHONE=%d CLOCK=%d FRONT=%d REAR=%d remaining=%llu gaps=%lu drops=%lu rejected=%lu missed=%lu\n",stateName(s.state),sendName(s.send),warningName(s.warning),s.imu,s.phone,s.clock,s.front,s.rear,(unsigned long long)s.remaining,(unsigned long)s.gaps,(unsigned long)s.dropped,(unsigned long)s.rejected,(unsigned long)s.missed);}
void serialCommand(const char* line){
 Command c{};bool valid=true;
 if(!strcmp(line,"help")){Serial.println("status | ack | close | BENCH: impact | candidate | imu on/off | cancel down/up | sos down/up | head present/absent/unknown | gps LAT LON");return;}
 if(!strcmp(line,"status")){printStatus();return;}
 if(!strcmp(line,"ack"))c.type=Command::Ack;
 else if(!strcmp(line,"close"))c.type=Command::Close;
#if RS_BENCH
 else if(!strcmp(line,"impact"))c.type=Command::Impact;
 else if(!strcmp(line,"candidate"))c.type=Command::Candidate;
 else if(!strcmp(line,"imu on")||!strcmp(line,"imu off")){c.type=Command::Imu;c.value=!strcmp(line,"imu on");}
 else if(!strcmp(line,"cancel down")||!strcmp(line,"cancel up")){c.type=Command::Cancel;c.value=!strcmp(line,"cancel down");}
 else if(!strcmp(line,"sos down")||!strcmp(line,"sos up")){c.type=Command::Sos;c.value=!strcmp(line,"sos down");}
 else if(!strcmp(line,"head present")||!strcmp(line,"head absent")||!strcmp(line,"head unknown")){c.type=Command::Head;c.value=!strcmp(line,"head present")?int(Presence::Present):(!strcmp(line,"head absent")?int(Presence::Absent):int(Presence::Unknown));}
 else if(!strncmp(line,"gps ",4)){c.type=Command::Gps;char extra;valid=sscanf(line+4,"%lf %lf %c",&c.lat,&c.lon,&extra)==2;}
#endif
 else valid=false;
 if(!valid)Serial.println("Invalid command; type help");else if(xQueueSend(commandQ,&c,0)!=pdTRUE)Serial.println("Command queue full");
}
void networkTask(void*){
 WiFi.mode(WIFI_AP);WiFi.setSleep(false);char ssid[40];snprintf(ssid,sizeof ssid,"RideShield-%04lx-%s",(unsigned long)(deviceId&0xffff),RS_BENCH?"BENCH":"CORE");networkFault=!WiFi.softAP(ssid,RS_WIFI_PASSWORD,6,false,4);
 Serial.printf("AP %s / WebSocket port 81 / %s\n",ssid,WiFi.softAPIP().toString().c_str());
 WebSocketsServer ws(81);PhoneGate gate;uint8_t client=255;bool authenticated=false;uint32_t session=0,syncId=0,rateCount=0;
 Ms connectedAt=0,syncAt=0,rateAt=0,published=0,statusAt=0;
 auto sync=[&](){syncAt=clockMs();gate.requestSync(++syncId,syncAt);char text[100];snprintf(text,sizeof text,"{\"type\":\"sync\",\"id\":%lu,\"controller_ms\":%llu}",(unsigned long)syncId,(unsigned long long)syncAt);ws.sendTXT(client,text);};
 ws.begin();ws.onEvent([&](uint8_t number,WStype_t type,uint8_t* payload,size_t length){
  const Ms now=clockMs();
  if(type==WStype_CONNECTED){if(client!=255){ws.disconnect(number);return;}client=number;authenticated=false;session=esp_random();if(!session)session=1;connectedAt=now;rateAt=now;rateCount=0;char welcome[100];snprintf(welcome,sizeof welcome,"{\"type\":\"welcome\",\"version\":1,\"session\":%lu}",(unsigned long)session);ws.sendTXT(client,welcome);return;}
  if(number!=client)return;
  if(type==WStype_DISCONNECTED){gate.close();client=255;authenticated=false;return;}
  if(type==WStype_PING||type==WStype_PONG)return;
  if(type!=WStype_BIN){ws.disconnect(number);return;}
  if(now-rateAt>=1000){rateAt=now;rateCount=0;}if(++rateCount>50){ws.disconnect(number);return;}
  Packet p;if(!decodePacket(payload,length,p)){ws.disconnect(number);return;}
  if(!authenticated){uint8_t diff=0;for(unsigned i=0;i<32;++i)diff|=p.token[i]^uint8_t(RS_PHONE_TOKEN[i]);
   if(p.type!=PacketType::Auth||p.session!=session||p.sequence!=1||diff){ws.disconnect(number);return;}
   authenticated=true;gate.open(session,now);sync();return;
  }
  Reject r=gate.accept(p,now);if(r!=Reject::None){char response[64];snprintf(response,sizeof response,"{\"type\":\"rejected\",\"code\":%u}",unsigned(r));ws.sendTXT(number,response);}
 });
 HardwareSerial gpsUart(1);TinyGPSPlus gps;
#if RS_ENABLE_GPS
 gpsUart.begin(board::GPS_BAUD,SERIAL_8N1,board::GPS_RX,board::GPS_TX);
#endif
 char line[96];size_t lineLength=0;bool overflow=false;
 for(;;){
  ws.loop();const Ms now=clockMs();
  if(client!=255){if((!authenticated&&now-connectedAt>5000)||(authenticated&&now-connectedAt>5000&&!gate.online(now)))ws.disconnect(client);else if(authenticated&&now-syncAt>=10000)sync();}
  if(now-published>=20){VisionPair p{gate.view(Source::Front,now),gate.view(Source::Rear,now),gate.online(now),gate.clockValid(now),gate.rejected(),now};xQueueOverwrite(visionQ,&p);published=now;}
  if(authenticated&&client!=255&&now-statusAt>=1000){auto s=statusSnapshot();char text[256];snprintf(text,sizeof text,"{\"type\":\"status\",\"profile\":\"%s\",\"state\":\"%s\",\"send\":\"%s\",\"warning\":\"%s\",\"front_valid\":%s,\"rear_valid\":%s}",RS_BENCH?"BENCH":"HARDWARE",stateName(s.state),sendName(s.send),warningName(s.warning),s.front?"true":"false",s.rear?"true":"false");ws.sendTXT(client,text);statusAt=now;}
#if RS_ENABLE_GPS
  for(unsigned i=0;i<256&&gpsUart.available();++i)gps.encode(char(gpsUart.read()));
  if(gps.location.isUpdated()&&gps.location.isValid()){const auto age=gps.location.age();double lat=gps.location.lat(),lon=gps.location.lng();if(age<=now){GpsFix f{true,lat,lon,now-age};xQueueOverwrite(gpsQ,&f);}}
#endif
  for(unsigned i=0;i<64&&Serial.available();++i){char c=Serial.read();if(c=='\r')continue;if(c=='\n'){if(!overflow){line[lineLength]=0;serialCommand(line);}else Serial.println("Command too long");lineLength=0;overflow=false;}else if(lineLength+1<sizeof line&&!overflow)line[lineLength++]=c;else overflow=true;}
  vTaskDelay(pdMS_TO_TICKS(5));
 }
}
void fatal(const char* message){applyOutput({});Serial.println(message);for(;;)delay(1000);}
}
void setup(){
 Serial.begin(115200);pinMode(board::CANCEL,INPUT_PULLUP);pinMode(board::SOS,INPUT_PULLUP);
#if RS_ENABLE_OUTPUTS
 for(int p:{board::BUZZER,board::LEFT_MOTOR,board::RIGHT_MOTOR,board::LED})if(p>=0){digitalWrite(p,LOW);pinMode(p,OUTPUT);}
#endif
 deviceId=uint32_t(ESP.getEfuseMac());bootId=esp_random();
 visionQ=xQueueCreate(1,sizeof(VisionPair));smsQ=xQueueCreate(1,sizeof(SmsJob));resultQ=xQueueCreate(1,sizeof(Result));journalQ=xQueueCreate(1,sizeof(Journal));eventQ=xQueueCreate(128,sizeof(Event));captureQ=xQueueCreate(1,sizeof(Capture));captureDoneQ=xQueueCreate(1,sizeof(bool));commandQ=xQueueCreate(16,sizeof(Command));gpsQ=xQueueCreate(1,sizeof(GpsFix));
 if(!visionQ||!smsQ||!resultQ||!journalQ||!eventQ||!captureQ||!captureDoneQ||!commandQ||!gpsQ)fatal("Queue allocation failed");
 size_t preCount=2001,eventCount=8002;Ms postMs=30000;
 auto* pre=static_cast<MotionRecord*>(heap_caps_malloc(preCount*sizeof(MotionRecord),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 auto* event=static_cast<MotionRecord*>(heap_caps_malloc(eventCount*sizeof(MotionRecord),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 if(!pre||!event){heap_caps_free(pre);heap_caps_free(event);preCount=200;eventCount=1402;postMs=6000;pre=static_cast<MotionRecord*>(heap_caps_malloc(preCount*sizeof(MotionRecord),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));event=static_cast<MotionRecord*>(heap_caps_malloc(eventCount*sizeof(MotionRecord),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));}
 if(!pre||!event){heap_caps_free(pre);heap_caps_free(event);pre=nullptr;event=nullptr;preCount=eventCount=0;captureFault=true;}
 for(size_t i=0;i<preCount;++i)new(pre+i)MotionRecord;for(size_t i=0;i<eventCount;++i)new(event+i)MotionRecord;
 alignas(BlackBox) static uint8_t boxMemory[sizeof(BlackBox)];recorder=new(boxMemory)BlackBox(pre,preCount,event,eventCount,postMs,postMs<30000);
 core.begin(clockMs(),deviceId,bootId);
 Preferences prefs;if(prefs.begin(board::JOURNAL,true)){if(prefs.isKey("pending")){Journal j{};Checkpoint cp;if(prefs.getBytesLength("pending")==sizeof j.bytes&&prefs.getBytes("pending",j.bytes,sizeof j.bytes)==sizeof j.bytes&&decodeCheckpoint(j.bytes,sizeof j.bytes,cp))core.restore(cp,clockMs());else recoveryFault=true;}prefs.end();}
#if !RS_BENCH
 imu.begin(board::SDA,board::SCL,board::MPU_ADDRESS);
#endif
 esp_task_wdt_init(5,true);
 if(xTaskCreatePinnedToCore(journalTask,"journal",4096,nullptr,2,nullptr,0)!=pdPASS||xTaskCreatePinnedToCore(storageTask,"storage",6144,nullptr,1,nullptr,0)!=pdPASS||xTaskCreatePinnedToCore(modemTask,"modem",6144,nullptr,2,nullptr,0)!=pdPASS||xTaskCreatePinnedToCore(networkTask,"network",8192,nullptr,1,nullptr,0)!=pdPASS||xTaskCreatePinnedToCore(controllerTask,"controller",8192,nullptr,5,nullptr,1)!=pdPASS)fatal("Task allocation failed; restart required");
}
void loop(){delay(1000);}
