#pragma once
#include <Wire.h>
#include "core.h"
// MPU-9250: 200 Hz accel+gyro FIFO; timestamps estimated from FIFO order.
// The AK8963 magnetometer is intentionally unused in crash bring-up.
class Mpu9250 {
 uint8_t address_=0x68,phase_=0,identity_=0;bool ready_=false;
 rs::Ms retryAt_=0,lastFrame_=0;
 bool write(uint8_t r,uint8_t v){Wire.beginTransmission(address_);Wire.write(r);Wire.write(v);return Wire.endTransmission()==0;}
 bool read(uint8_t r,uint8_t* b,uint8_t n){
  Wire.beginTransmission(address_);Wire.write(r);if(Wire.endTransmission(false)!=0)return false;
  if(Wire.requestFrom(address_,n,uint8_t(true))!=n)return false;
  for(unsigned i=0;i<n;++i)b[i]=Wire.read();return true;
 }
 int fault(rs::Ms now){ready_=false;phase_=0;retryAt_=now+100;return -1;}
public:
 uint8_t identity() const{return identity_;}
 void begin(int sda,int scl,uint8_t a){address_=a;Wire.begin(sda,scl,400000);Wire.setTimeOut(3);}
 int poll(rs::Ms now,rs::Motion* out,size_t capacity){
  if(!ready_){
   if(now<retryAt_)return 0;
   if(phase_==0){identity_=0;uint8_t id=0;
    if(!read(0x75,&id,1)){retryAt_=now+2000;return -1;}
    identity_=id;
    if(id!=0x71||!write(0x6b,0x80)){retryAt_=now+2000;return -1;}
    phase_=1;retryAt_=now+100;return 0;}
   const uint8_t regs[][2]={{0x6b,1},{0x6c,0},{0x1a,3},{0x19,4},{0x1b,0x18},{0x1c,0x18},{0x23,0},{0x6a,4},{0x6a,0x40},{0x23,0x78},{0x38,0x10}};
   for(const auto& r:regs)if(!write(r[0],r[1]))return fault(now);
   uint8_t cfg[4];if(!read(0x19,cfg,4)||cfg[0]!=4||cfg[1]!=3||cfg[2]!=0x18||cfg[3]!=0x18)return fault(now);
   ready_=true;lastFrame_=now;return 0;
  }
  uint8_t status=0,count[2];if(!read(0x3a,&status,1)||!read(0x72,count,2))return fault(now);
  unsigned bytes=(unsigned(count[0])<<8)|count[1],frames=bytes/12;
  // The MPU-9250 FIFO holds 512 bytes, unlike the MPU-6050's 1024 bytes.
  if((status&0x10)||bytes>=512||bytes%12||frames>capacity)return fault(now);
  if(!frames)return now-lastFrame_>100?fault(now):0;
  for(unsigned i=0;i<frames;++i){
   uint8_t b[12];if(!read(0x74,b,12))return fault(now);
   int16_t v[6];for(unsigned j=0;j<6;++j)v[j]=int16_t((uint16_t(b[j*2])<<8)|b[j*2+1]);
   auto& m=out[i];m={};m.ms=now-5*(frames-1-i);m.ax=v[0]/2048.f;m.ay=v[1]/2048.f;m.az=v[2]/2048.f;
   m.gx=v[3]/16.4f;m.gy=v[4]/16.4f;m.gz=v[5]/16.4f;
  }lastFrame_=now;return int(frames);
 }
};
