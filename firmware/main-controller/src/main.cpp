#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsServer.h>
#include <WebServer.h>
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
#include <algorithm>
#include "board_config.h"
#include "core.h"
#include "codec.h"
#include "phone.h"
#include "blackbox.h"
#include "sms_modem.h"
#include "mpu9250.h"
#include "tof_sensor.h"
#include "rider_inputs.h"
#include "dashboard.h"
using namespace rs;
namespace {
Ms clockMs(){return esp_timer_get_time()/1000;}
Core core;Mpu9250 imu;BlackBox* recorder=nullptr;
TofSensor tof;RiderInputs rider(RS_MQ_WARMUP_MS);RiderCalibration calibration{};
uint32_t deviceId,bootId;
struct VisionPair{Vision front,rear;bool online,clock;uint32_t rejected;Ms published;};
struct Result{Id id;uint32_t attempt;SendResult outcome;};
struct Journal{uint8_t bytes[CHECKPOINT_SIZE];};
struct Capture{const MotionRecord* records;size_t count;Id id;bool truncated;};
struct Command{enum Type{Impact,Candidate,Imu,Cancel,Sos,Head,Ack,Close,Gps,HeadWorn,HeadEmpty,IrOpen,IrClosed,MqLimit,CalClear}type;int value=0;double lat=0,lon=0;};
struct GpsTelemetry { GpsFix fix{};float speedKmh=0;uint32_t satellites=0;bool speedValid=false; };
struct ModemTelemetry { bool online=false,simReady=false,registered=false,busy=false;Ms at=0; };
struct Status {
 Ms now=0,remaining=0,lastMotionAt=0;
 State state=State::Degraded;SendState send=SendState::None;
 const char* warning="SYSTEM";
 bool imu=false,phone=false,clock=false,front=false,rear=false;
 uint8_t imuId=0,irRaw=0,tofStatus=255;
 float accelG=0,gyroDps=0;
 uint32_t gaps=0,dropped=0,rejected=0,missed=0,loopMaxUs=0;
 RiderView rider{};RiderCalibration cal{};GpsTelemetry gps{};ModemTelemetry modem{};
 Incident incident{};
};
QueueHandle_t visionQ,smsQ,resultQ,journalQ,eventQ,captureQ,captureDoneQ,commandQ,gpsQ,calQ,modemQ;
std::atomic<bool> sdFault{false},journalFault{false},captureFault{false},networkFault{false},recoveryFault{false};
std::atomic<bool> sdReady{false},calibrationFault{false},calibrationPending{false};
std::atomic<uint32_t> eventDrops{0};
portMUX_TYPE statusMux=portMUX_INITIALIZER_UNLOCKED;Status sharedStatus{};
Status statusSnapshot();
void encodeCalibration(const RiderCalibration& c,uint8_t* b){
 memset(b,0,20);memcpy(b,"RSC1",4);b[4]=c.wornMm;b[5]=c.wornMm>>8;b[6]=c.emptyMm;b[7]=c.emptyMm>>8;
 b[8]=uint8_t(c.irOpen);b[9]=uint8_t(c.irClosed);b[10]=c.mqLimitMv;b[11]=c.mqLimitMv>>8;write32(b+16,crc32(b,16));
}
bool decodeCalibration(const uint8_t* b,RiderCalibration& c){
 if(memcmp(b,"RSC1",4)||b[12]||b[13]||b[14]||b[15]||read32(b+16)!=crc32(b,16))return false;
 RiderCalibration v{uint16_t(b[4]|b[5]<<8),uint16_t(b[6]|b[7]<<8),int8_t(b[8]),int8_t(b[9]),uint16_t(b[10]|b[11]<<8)};
 if((v.wornMm && (v.wornMm<10||v.wornMm>2000))||(v.emptyMm&&(v.emptyMm<10||v.emptyMm>2000)) ||
   (v.wornMm&&v.emptyMm&&!v.headReady()) || v.irOpen< -1||v.irOpen>1||v.irClosed< -1||v.irClosed>1 ||
   (v.irOpen>=0&&v.irClosed>=0&&!v.eyeReady()) || (v.mqLimitMv&&!v.mqReady()))return false;
 c=v;return true;
}
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
 float lastAccelG=0,lastGyroDps=0;Ms lastMotionAt=0;
 uint32_t journalRevision=UINT32_MAX;Id captured{};VisionPair pair{};
 RiderView riderView{};GpsTelemetry gpsView{};ModemTelemetry modemView{};uint8_t tofStatus=255;Ms nextMq=0;uint32_t loopMax=0;
 for(;;){
  const auto loopStart=esp_timer_get_time();const Ms now=clockMs();Command cmd;
  for(unsigned i=0;i<4&&xQueueReceive(commandQ,&cmd,0)==pdTRUE;++i){
   if(cmd.type==Command::Ack)core.acknowledge(now);
   else if(cmd.type==Command::Close)core.closeIncident(now);
   else if(cmd.type>=Command::HeadWorn){
    auto c=calibration;bool ok=true;
    if(cmd.type==Command::CalClear)c={};
    else if(cmd.type==Command::HeadWorn||cmd.type==Command::HeadEmpty){
     ok=riderView.tofFresh&&riderView.tofStable;
     if(ok){if(cmd.type==Command::HeadWorn)c.wornMm=riderView.rangeMm;else c.emptyMm=riderView.rangeMm;
      if(c.wornMm&&c.emptyMm)ok=c.headReady();}
    }else if(cmd.type==Command::IrOpen)c.irOpen=digitalRead(board::IR_SIGNAL);
    else if(cmd.type==Command::IrClosed)c.irClosed=digitalRead(board::IR_SIGNAL);
    else if(cmd.type==Command::MqLimit){c.mqLimitMv=cmd.value;ok=c.mqReady();}
    if(c.irOpen>=0&&c.irClosed>=0&&!c.eyeReady())ok=false;
    if(ok){calibration=c;rider.calibrate(c);calibrationPending=true;xQueueOverwrite(calQ,&c);Serial.println("Calibration accepted; saving");}
    else Serial.println("Calibration rejected: stable valid ranges, separated head distances, distinct IR states, or MQ 100..3000 mV required");
   }
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
#if RS_ENABLE_TOF && !RS_BENCH
  TofSensor::Sample range;if(tof.poll(now,range)){tofStatus=range.status;rider.range(range.mm,range.valid,now);}
#endif
#if RS_ENABLE_MQ
  if(now>=nextMq){rider.alcoholSample(analogReadMilliVolts(board::MQ_ANALOG),true,now);nextMq=now+100;}
#endif
  riderView=rider.tick(now,RS_ENABLE_STRAP&&digitalRead(board::STRAP)==LOW,RS_ENABLE_IR&&digitalRead(board::IR_SIGNAL)==HIGH);
#if !RS_BENCH
  core.head(riderView.head,now);
#endif
  core.tick(now,cancel||digitalRead(board::CANCEL)==LOW,sos||digitalRead(board::SOS)==LOW);
  Motion samples[4];int count=0;
#if RS_BENCH
  if(benchImu){samples[0].ms=now;if(impactFrames){samples[0].ax=impactG;--impactFrames;}count=1;}else core.imuUnavailable(now);
#else
  count=imu.poll(now,samples,4);if(count<0)core.imuUnavailable(now);
#endif
  for(int i=0;i<count;++i){
   const auto& sample=samples[i];
   lastAccelG=std::sqrt(sample.ax*sample.ax+sample.ay*sample.ay+sample.az*sample.az);
   lastGyroDps=std::sqrt(sample.gx*sample.gx+sample.gy*sample.gy+sample.gz*sample.gz);
   lastMotionAt=now;
   if(core.motion(samples[i],now)){
    const auto& m=samples[i];bool clipped=std::fabs(m.ax)>=15.68f||std::fabs(m.ay)>=15.68f||std::fabs(m.az)>=15.68f||std::fabs(m.gx)>=1960||std::fabs(m.gy)>=1960||std::fabs(m.gz)>=1960;
    recorder->push(m,(RS_BENCH?1u:4u)|(clipped?2u:0u));
    const Id id=core.incident().id;if(id.sequence&&!(id==captured)){recorder->trigger(id,now);captured=id;}
   }
  }
  const Id id=core.incident().id;if(id.sequence&&!(id==captured)){recorder->trigger(id,now);captured=id;}
  if(xQueuePeek(visionQ,&pair,0)==pdTRUE){core.vision(pair.front,false);core.vision(pair.rear,true);}
  if(xQueueReceive(gpsQ,&gpsView,0)==pdTRUE)core.gps(gpsView.fix,now);
  xQueuePeek(modemQ,&modemView,0);
  Result result;if(xQueueReceive(resultQ,&result,0)==pdTRUE)core.smsResult(result.id,result.attempt,result.outcome,now);
  core.systemFault(sdFault||journalFault||captureFault||networkFault||recoveryFault||calibrationFault);
  const auto output=combineWarnings(core.output(now),riderView,now);applyOutput(output.pins);
  if(core.revision()!=journalRevision){Journal j{};encodeCheckpoint(core.checkpoint(),j.bytes);xQueueOverwrite(journalQ,&j);journalRevision=core.revision();}
  SmsJob job;if(uxQueueSpacesAvailable(smsQ)&&core.takeSms(now,job))xQueueSend(smsQ,&job,0);
  Event event;while(core.popEvent(event))if(xQueueSend(eventQ,&event,0)!=pdTRUE)++eventDrops;
  bool done;if(xQueueReceive(captureDoneQ,&done,0)==pdTRUE)recorder->release();
  recorder->tick(now);Capture capture;
  if(recorder->acquire(capture.records,capture.count,capture.id,capture.truncated)&&xQueueSend(captureQ,&capture,0)!=pdTRUE){captureFault=true;recorder->release();}
  const bool fresh=now>=pair.published&&now-pair.published<=3000;
  loopMax=std::max(loopMax,uint32_t(esp_timer_get_time()-loopStart));
  Status status;status.now=now;status.remaining=core.countdownRemaining(now);status.lastMotionAt=lastMotionAt;
  status.state=core.state();status.send=core.sendState();status.warning=output.label;status.imu=core.imuHealthy();
  status.phone=fresh&&pair.online;status.clock=fresh&&pair.clock;status.front=pair.front.valid&&now<pair.front.expiresMs;status.rear=pair.rear.valid&&now<pair.rear.expiresMs;
  status.imuId=imu.identity();status.irRaw=digitalRead(board::IR_SIGNAL);status.tofStatus=tofStatus;
  status.accelG=lastAccelG;status.gyroDps=lastGyroDps;status.gaps=core.sampleGaps();status.dropped=core.droppedEvents()+eventDrops.load();status.rejected=pair.rejected;status.missed=recorder->missedCaptures();status.loopMaxUs=loopMax;
  status.rider=riderView;status.cal=calibration;status.gps=gpsView;status.modem=modemView;status.incident=core.incident();
  portENTER_CRITICAL(&statusMux);sharedStatus=status;portEXIT_CRITICAL(&statusMux);
  esp_task_wdt_reset();vTaskDelayUntil(&wake,pdMS_TO_TICKS(5));
 }
}
void journalTask(void*){
 Preferences prefs;bool opened=prefs.begin(board::JOURNAL,false);journalFault=!opened;Journal latest{};bool pending=false;
 for(;;){
  if(xQueueReceive(journalQ,&latest,pdMS_TO_TICKS(500))==pdTRUE)pending=true;
  if(!opened){opened=prefs.begin(board::JOURNAL,false);journalFault=!opened;}
  RiderCalibration c;if(xQueueReceive(calQ,&c,0)==pdTRUE){uint8_t bytes[20];encodeCalibration(c,bytes);bool ok=opened&&prefs.putBytes("calibration",bytes,sizeof bytes)==sizeof bytes;calibrationFault=!ok;calibrationPending=false;Serial.println(ok?"Calibration saved":"Calibration save failed; repeat calibration");}
  if(opened&&pending){const bool ok=prefs.putBytes("pending",latest.bytes,sizeof latest.bytes)==sizeof latest.bytes;journalFault=!ok;if(ok)pending=false;}
 }
}
void storageTask(void*){
 File events,telemetry;
#if RS_ENABLE_SD
 SPI.begin(board::SD_SCK,board::SD_MISO,board::SD_MOSI,board::SD_CS);
 if(!SD.begin(board::SD_CS,SPI,4000000))sdFault=true;
 else {char path[48];snprintf(path,sizeof path,"/events-%08lx.rsl",(unsigned long)bootId);events=SD.open(path,FILE_APPEND);if(!events)sdFault=true;else sdReady=true;}
#endif
 Ms flushed=0,nextMount=0,nextTelemetry=0;
 for(;;){
#if RS_ENABLE_SD
  const Ms now=clockMs();
  if(sdFault&&now>=nextMount){events.close();telemetry.close();SD.end();nextMount=now+5000;
   if(SD.begin(board::SD_CS,SPI,4000000)){char path[48];snprintf(path,sizeof path,"/events-%08lx.rsl",(unsigned long)bootId);events=SD.open(path,FILE_APPEND);sdFault=!events;}
  }
  if(!sdFault&&!telemetry){char path[48];snprintf(path,sizeof path,"/sensors-%08lx.csv",(unsigned long)bootId);telemetry=SD.open(path,FILE_APPEND);
   if(!telemetry)sdFault=true;else if(!telemetry.size())telemetry.println("ms,state,alert,accel_g,gyro_dps,tof_mm,tof_valid,head,wear,ir,mq_mv,alcohol,lat,lon,gps_valid,gps_age_ms,gaps");}
  if(telemetry&&!sdFault&&now>=nextTelemetry){auto s=statusSnapshot();char row[320];
   int len=snprintf(row,sizeof row,"%llu,%s,%s,%.3f,%.1f,%u,%d,%s,%s,%u,%u,%s,%.6f,%.6f,%d,%llu,%lu\n",(unsigned long long)s.now,stateName(s.state),s.warning,s.accelG,s.gyroDps,s.rider.rangeMm,s.rider.tofFresh,presenceName(s.rider.head),wearName(s.rider.wear),s.irRaw,s.rider.mqMv,alcoholName(s.rider.alcohol),s.gps.fix.lat,s.gps.fix.lon,s.gps.fix.valid,(unsigned long long)(s.gps.fix.valid?s.now-s.gps.fix.ms:0),(unsigned long)s.gaps);
   if(len<=0||len>=int(sizeof row)||telemetry.write((const uint8_t*)row,len)!=size_t(len))sdFault=true;
   nextTelemetry=now+1000;
  }
#endif
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
  sdReady=RS_ENABLE_SD&&!sdFault.load()&&bool(events);
  if(events&&clockMs()-flushed>=1000){events.flush();if(telemetry)telemetry.flush();flushed=clockMs();}vTaskDelay(pdMS_TO_TICKS(10));
 }
}
void modemTask(void*){
 HardwareSerial uart(2);
#if RS_ENABLE_LTE
 uart.begin(board::LTE_BAUD,SERIAL_8N1,board::LTE_RX,board::LTE_TX);
#endif
 SmsModem modem;SmsJob job{};bool active=false;
 ModemTelemetry health{};Ms nextHealth=0,deadline=0,lastRx=0;unsigned probe=0;bool probing=false;
 char response[192]{};size_t used=0;
 auto publish=[&](){health.at=clockMs();health.busy=active||probing;xQueueOverwrite(modemQ,&health);};
 for(;;){
  const Ms now=clockMs();
#if RS_ENABLE_LTE
  // The modem task owns UART2. Finish each health query before handing it to SMS.
  if(!active){
   for(unsigned n=0;n<256&&uart.available();++n){char ch=uart.read();lastRx=now;if(used+1<sizeof response){response[used++]=ch;response[used]=0;}}
   if(probing&&(strstr(response,"\r\nOK\r\n")||strstr(response,"ERROR")||now>=deadline)){
    bool ok=strstr(response,"\r\nOK\r\n")!=nullptr;
    if(probe==0)health.online=ok;
    else if(probe==1)health.simReady=ok&&strstr(response,"READY");
    else {int mode=-1,reg=-1;const char* r=strstr(response,"+CEREG:");if(r)sscanf(r,"+CEREG: %d,%d",&mode,&reg);health.registered=ok&&(reg==1||reg==5);}
    probing=false;probe=(probe+1)%3;nextHealth=now+(probe?200:15000);publish();
   }
   if(!probing&&now>=nextHealth&&now-lastRx>=100&&uxQueueMessagesWaiting(smsQ)==0){
    used=0;response[0]=0;uart.print(probe==0?"AT\r":probe==1?"AT+CPIN?\r":"AT+CEREG?\r");probing=true;deadline=now+2000;publish();
   }
  }
#endif
  if(!active&&!probing&&now-lastRx>=100&&xQueueReceive(smsQ,&job,0)==pdTRUE){
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
  }publish();vTaskDelay(pdMS_TO_TICKS(5));
 }
}
Status statusSnapshot(){portENTER_CRITICAL(&statusMux);Status s=sharedStatus;portEXIT_CRITICAL(&statusMux);return s;}
void printStatus(){auto s=statusSnapshot();Serial.printf("STATE=%s SEND=%s ALERT=%s IMU=%d id=0x%02x a=%.2fg gyro=%.0f TOF=%umm valid=%d status=%u HEAD=%s WEAR=%s IR=%u EYE=%d MQ=%umV ALCOHOL=%s PHONE=%d remaining=%llu gaps=%lu SD=%d cal_pending=%d loop_max_us=%lu\n",stateName(s.state),sendName(s.send),s.warning,s.imu,s.imuId,s.accelG,s.gyroDps,s.rider.rangeMm,s.rider.tofFresh,s.tofStatus,presenceName(s.rider.head),wearName(s.rider.wear),s.irRaw,s.rider.eyeProlonged,s.rider.mqMv,alcoholName(s.rider.alcohol),s.phone,(unsigned long long)s.remaining,(unsigned long)s.gaps,sdReady.load(),calibrationPending.load(),(unsigned long)s.loopMaxUs);}
String statusJson(){
 auto s=statusSnapshot();String j;j.reserve(2000);j="{";
 auto str=[&](const char*k,const char*v){j+='"';j+=k;j+="\":\"";j+=v;j+="\",";};
 auto num=[&](const char*k,double v,unsigned decimals=0){j+='"';j+=k;j+="\":";j+=String(v,decimals);j+=',';};
 auto flag=[&](const char*k,bool v){j+='"';j+=k;j+="\":";j+=v?"true,":"false,";};
 str("state",stateName(s.state));str("send",sendName(s.send));str("warning",s.warning);
 flag("imu",s.imu);num("accel_g",s.accelG,2);num("gyro_dps",s.gyroDps,1);num("gaps",s.gaps);num("event_index",eventIndex(s.incident));
 str("head",presenceName(s.rider.head));str("wear",wearName(s.rider.wear));flag("strap_closed",s.rider.strapClosed);
 num("tof_mm",s.rider.rangeMm);flag("tof_valid",s.rider.tofFresh);num("tof_status",s.tofStatus);flag("head_calibrated",s.cal.headReady());
 num("ir_raw",s.irRaw);flag("ir_calibrated",s.cal.eyeReady());flag("eye_signal",s.rider.eyeProlonged);num("mq_mv",s.rider.mqMv);str("alcohol",alcoholName(s.rider.alcohol));
 flag("pre_ride_ready",s.imu&&s.rider.wear==Wear::Worn&&s.cal.eyeReady()&&!s.rider.eyeProlonged&&s.rider.alcohol==Alcohol::Clear);
 const auto age=s.gps.fix.valid&&s.now>=s.gps.fix.ms?s.now-s.gps.fix.ms:0;
 flag("gps_fresh",s.gps.fix.valid&&age<=10000);num("latitude",s.gps.fix.lat,6);num("longitude",s.gps.fix.lon,6);num("gps_age_ms",age);num("speed_kmh",s.gps.speedKmh,1);
 num("remaining_ms",s.remaining);flag("sms_enabled",RS_ALLOW_SMS);flag("modem_online",s.modem.online&&s.now>=s.modem.at&&s.now-s.modem.at<3000);flag("modem_registered",s.modem.online&&s.modem.simReady&&s.modem.registered&&s.now>=s.modem.at&&s.now-s.modem.at<3000);flag("sd_ready",sdReady.load());
 flag("phone",s.phone);flag("clock",s.clock);num("clients",WiFi.softAPgetStationNum());num("free_heap",ESP.getFreeHeap());num("loop_max_us",s.loopMaxUs);
 flag("calibration_pending",calibrationPending.load());flag("calibration_fault",calibrationFault.load());num("uptime_ms",s.now);
 flag("sd_fault",sdFault.load());flag("journal_fault",journalFault.load());flag("recovery_fault",recoveryFault.load());flag("capture_fault",captureFault.load());flag("network_fault",networkFault.load());
 flag("event_clipped",s.incident.clipped);num("event_sample_gaps",s.incident.sampleGaps);num("event_drops",s.dropped);num("capture_misses",s.missed);
 j.remove(j.length()-1);j+='}';return j;
}
void serialCommand(const char* line){
 Command c{};bool valid=true;
 if(!strcmp(line,"help")){Serial.println("status | ack | close | headcal worn | headcal empty | ircal open | ircal closed | mqlimit 100..3000 | calclear");
#if RS_BENCH
 Serial.println("BENCH: impact | candidate | imu on/off | cancel down/up | sos down/up | head present/absent/unknown | gps LAT LON");
#endif
 return;}
 if(!strcmp(line,"status")){printStatus();return;}
 if(!strcmp(line,"ack"))c.type=Command::Ack;
 else if(!strcmp(line,"close"))c.type=Command::Close;
 else if(!strcmp(line,"headcal worn"))c.type=Command::HeadWorn;
 else if(!strcmp(line,"headcal empty"))c.type=Command::HeadEmpty;
 else if(!strcmp(line,"ircal open"))c.type=Command::IrOpen;
 else if(!strcmp(line,"ircal closed"))c.type=Command::IrClosed;
 else if(!strcmp(line,"calclear"))c.type=Command::CalClear;
 else if(!strncmp(line,"mqlimit ",8)){c.type=Command::MqLimit;char extra;valid=sscanf(line+8,"%d %c",&c.value,&extra)==1&&c.value>=100&&c.value<=3000;}
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
 WiFi.mode(WIFI_AP);WiFi.setSleep(false);char ssid[40];snprintf(ssid,sizeof ssid,"RideShield-%04lx-%s",(unsigned long)(deviceId&0xffff),RS_BENCH?"BENCH":"CORE");
 // Keep static camera addresses .2/.3 outside the phone/laptop DHCP pool.
 networkFault=!WiFi.softAPConfig(IPAddress(192,168,4,1),IPAddress(192,168,4,1),IPAddress(255,255,255,0),IPAddress(192,168,4,20));
 networkFault=networkFault||!WiFi.softAP(ssid,RS_WIFI_PASSWORD,6,false,4);
 Serial.printf("AP %s / WebSocket port 81 / %s\n",ssid,WiFi.softAPIP().toString().c_str());
 WebServer http(80);http.on("/",HTTP_GET,[&](){http.send_P(200,"text/html",DASHBOARD_HTML);});http.on("/api/status",HTTP_GET,[&](){http.sendHeader("Cache-Control","no-store");http.send(200,"application/json",statusJson());});http.begin();
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
  http.handleClient();ws.loop();const Ms now=clockMs();
  if(client!=255){if((!authenticated&&now-connectedAt>5000)||(authenticated&&now-connectedAt>5000&&!gate.online(now)))ws.disconnect(client);else if(authenticated&&now-syncAt>=10000)sync();}
  if(now-published>=20){VisionPair p{gate.view(Source::Front,now),gate.view(Source::Rear,now),gate.online(now),gate.clockValid(now),gate.rejected(),now};xQueueOverwrite(visionQ,&p);published=now;}
  if(authenticated&&client!=255&&now-statusAt>=1000){auto s=statusSnapshot();char text[256];snprintf(text,sizeof text,"{\"type\":\"status\",\"profile\":\"%s\",\"state\":\"%s\",\"send\":\"%s\",\"warning\":\"%s\",\"front_valid\":%s,\"rear_valid\":%s}",RS_BENCH?"BENCH":"HARDWARE",stateName(s.state),sendName(s.send),s.warning,s.front?"true":"false",s.rear?"true":"false");ws.sendTXT(client,text);statusAt=now;}
#if RS_ENABLE_GPS
  for(unsigned i=0;i<256&&gpsUart.available();++i)gps.encode(char(gpsUart.read()));
  if(gps.location.isUpdated()&&gps.location.isValid()){const auto age=gps.location.age();double lat=gps.location.lat(),lon=gps.location.lng();if(age<=now){GpsTelemetry f;f.fix={true,lat,lon,now-age};f.speedValid=gps.speed.isValid()&&gps.speed.age()<10000;f.speedKmh=f.speedValid?gps.speed.kmph():0;f.satellites=gps.satellites.value();xQueueOverwrite(gpsQ,&f);}}
#endif
  for(unsigned i=0;i<64&&Serial.available();++i){char c=Serial.read();if(c=='\r')continue;if(c=='\n'){if(!overflow){line[lineLength]=0;serialCommand(line);}else Serial.println("Command too long");lineLength=0;overflow=false;}else if(lineLength+1<sizeof line&&!overflow)line[lineLength++]=c;else overflow=true;}
  vTaskDelay(pdMS_TO_TICKS(5));
 }
}
void fatal(const char* message){applyOutput({});Serial.println(message);for(;;)delay(1000);}
}
void setup(){
 Serial.begin(115200);Serial.printf("RideShield NodeMCU-32S core; live SMS %s\n",RS_ALLOW_SMS?"enabled":"disabled");
 pinMode(board::CANCEL,INPUT_PULLUP);pinMode(board::SOS,INPUT_PULLUP);
 pinMode(board::STRAP,INPUT);
#if RS_ENABLE_MQ
 analogReadResolution(12);analogSetPinAttenuation(board::MQ_ANALOG,ADC_11db);
#endif
 pinMode(board::IR_SIGNAL,INPUT); // GPIO36 has no internal pull-up.
#if RS_ENABLE_OUTPUTS
 for(int p:{board::BUZZER,board::LEFT_MOTOR,board::RIGHT_MOTOR,board::LED})if(p>=0){digitalWrite(p,LOW);pinMode(p,OUTPUT);}
#endif
 deviceId=uint32_t(ESP.getEfuseMac());bootId=esp_random();
 visionQ=xQueueCreate(1,sizeof(VisionPair));smsQ=xQueueCreate(1,sizeof(SmsJob));resultQ=xQueueCreate(1,sizeof(Result));journalQ=xQueueCreate(1,sizeof(Journal));eventQ=xQueueCreate(128,sizeof(Event));captureQ=xQueueCreate(1,sizeof(Capture));captureDoneQ=xQueueCreate(1,sizeof(bool));commandQ=xQueueCreate(16,sizeof(Command));gpsQ=xQueueCreate(1,sizeof(GpsTelemetry));calQ=xQueueCreate(1,sizeof(RiderCalibration));modemQ=xQueueCreate(1,sizeof(ModemTelemetry));
 if(!visionQ||!smsQ||!resultQ||!journalQ||!eventQ||!captureQ||!captureDoneQ||!commandQ||!gpsQ||!calQ||!modemQ)fatal("Queue allocation failed");
 size_t preCount=2001,eventCount=8002;Ms postMs=30000;
 MotionRecord* pre=nullptr;MotionRecord* event=nullptr;
 // Most NodeMCU-32S boards have no PSRAM. Try it only when available.
 if(psramFound()){
  pre=static_cast<MotionRecord*>(heap_caps_malloc(preCount*sizeof(MotionRecord),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  event=static_cast<MotionRecord*>(heap_caps_malloc(eventCount*sizeof(MotionRecord),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 }
 if(!pre||!event){heap_caps_free(pre);heap_caps_free(event);preCount=200;eventCount=1402;postMs=6000;pre=static_cast<MotionRecord*>(heap_caps_malloc(preCount*sizeof(MotionRecord),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));event=static_cast<MotionRecord*>(heap_caps_malloc(eventCount*sizeof(MotionRecord),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));}
 if(!pre||!event){heap_caps_free(pre);heap_caps_free(event);pre=nullptr;event=nullptr;preCount=eventCount=0;captureFault=true;}
 Serial.printf("BLACKBOX pre=%u post=%u records (%u B each), PSRAM=%d, free heap=%u\n",unsigned(preCount),unsigned(eventCount),unsigned(sizeof(MotionRecord)),psramFound(),unsigned(ESP.getFreeHeap()));
 for(size_t i=0;i<preCount;++i)new(pre+i)MotionRecord;for(size_t i=0;i<eventCount;++i)new(event+i)MotionRecord;
 alignas(BlackBox) static uint8_t boxMemory[sizeof(BlackBox)];recorder=new(boxMemory)BlackBox(pre,preCount,event,eventCount,postMs,postMs<30000);
 core.begin(clockMs(),deviceId,bootId);
 Preferences prefs;if(prefs.begin(board::JOURNAL,true)){if(prefs.isKey("pending")){Journal j{};Checkpoint cp;if(prefs.getBytesLength("pending")==sizeof j.bytes&&prefs.getBytes("pending",j.bytes,sizeof j.bytes)==sizeof j.bytes&&decodeCheckpoint(j.bytes,sizeof j.bytes,cp))core.restore(cp,clockMs());else recoveryFault=true;}if(prefs.isKey("calibration")){uint8_t b[20];if(prefs.getBytesLength("calibration")!=sizeof b||prefs.getBytes("calibration",b,sizeof b)!=sizeof b||!decodeCalibration(b,calibration))calibrationFault=true;}prefs.end();}
 rider.begin(clockMs(),calibration);
#if !RS_BENCH
 imu.begin(board::SDA,board::SCL,board::MPU_ADDRESS);
#if RS_ENABLE_TOF
 Serial.printf("ToF continuous ranging: %s\n",tof.begin(board::TOF_ADDRESS)?"ready":"unavailable");
#endif
#endif
 esp_task_wdt_init(5,true);
 if(xTaskCreatePinnedToCore(journalTask,"journal",4096,nullptr,2,nullptr,0)!=pdPASS||xTaskCreatePinnedToCore(storageTask,"storage",6144,nullptr,1,nullptr,0)!=pdPASS||xTaskCreatePinnedToCore(modemTask,"modem",6144,nullptr,2,nullptr,0)!=pdPASS||xTaskCreatePinnedToCore(networkTask,"network",8192,nullptr,1,nullptr,0)!=pdPASS||xTaskCreatePinnedToCore(controllerTask,"controller",8192,nullptr,5,nullptr,1)!=pdPASS)fatal("Task allocation failed; restart required");
}
void loop(){delay(1000);}
