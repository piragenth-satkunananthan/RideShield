#include "codec.h"
#include <cmath>
#include <cstring>
namespace rs {
uint32_t read32(const uint8_t*p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
uint64_t read64(const uint8_t*p){return uint64_t(read32(p))|(uint64_t(read32(p+4))<<32);}
void write32(uint8_t*p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=uint8_t(v>>(8*i));}
void write64(uint8_t*p,uint64_t v){write32(p,uint32_t(v));write32(p+4,uint32_t(v>>32));}
static void putFloat(uint8_t*p,float f){uint32_t n;std::memcpy(&n,&f,4);write32(p,n);}
static float getFloat(const uint8_t*p){uint32_t n=read32(p);float f;std::memcpy(&f,&n,4);return f;}
uint32_t crc32(const uint8_t*p,size_t n){uint32_t c=0xffffffffu;for(size_t i=0;i<n;++i){c^=p[i];for(unsigned b=0;b<8;++b)c=(c>>1)^(0xedb88320u&uint32_t(-int32_t(c&1)));}return c^0xffffffffu;}
void encodeCheckpoint(const Checkpoint&v,uint8_t*b){
 std::memset(b,0,72);std::memcpy(b,"RSJ1",4);b[4]=uint8_t(v.state);b[5]=uint8_t(v.send);
 b[6]=(v.incident.manual?1:0)|(v.incident.uncertain?2:0)|(v.incident.clipped?4:0)|(v.incident.possibleDuplicate?8:0);
 write32(b+8,v.incident.id.device);write32(b+12,v.incident.id.boot);write32(b+16,v.incident.id.sequence);write64(b+20,v.incident.startedMs);
 putFloat(b+28,v.incident.peakG);putFloat(b+32,v.incident.peakDps);write32(b+36,v.incident.sampleGaps);write32(b+40,v.incident.clippedSamples);write32(b+68,crc32(b,68));
}
bool decodeCheckpoint(const uint8_t*b,size_t n,Checkpoint&v){
 if(!b||n!=72||std::memcmp(b,"RSJ1",4)||read32(b+68)!=crc32(b,68)||b[4]>uint8_t(State::Incident)||b[5]>uint8_t(SendState::Disabled)||(b[6]&0xf0)||b[7])return false;
 for(size_t i=44;i<68;++i)if(b[i])return false;
 Checkpoint o;o.state=State(b[4]);o.send=SendState(b[5]);o.incident.manual=b[6]&1;o.incident.uncertain=b[6]&2;o.incident.clipped=b[6]&4;o.incident.possibleDuplicate=b[6]&8;
 o.incident.id={read32(b+8),read32(b+12),read32(b+16)};o.incident.startedMs=read64(b+20);o.incident.peakG=getFloat(b+28);o.incident.peakDps=getFloat(b+32);
 o.incident.sampleGaps=read32(b+36);o.incident.clippedSamples=read32(b+40);
 if(!std::isfinite(o.incident.peakG)||!std::isfinite(o.incident.peakDps)||o.incident.peakG<0||o.incident.peakDps<0)return false;
 if(o.state==State::Incident&&o.send==SendState::None)return false;
 if(o.state==State::Countdown&&o.send!=SendState::None)return false;
 v=o;return true;
}
void encodeEvent(const Event&e,uint8_t*b){std::memset(b,0,48);std::memcpy(b,"RSE1",4);write32(b+4,48);write64(b+8,e.ms);write32(b+16,e.id.device);write32(b+20,e.id.boot);write32(b+24,e.id.sequence);write32(b+28,uint32_t(e.kind));putFloat(b+32,e.value);write32(b+36,e.detail);write32(b+44,crc32(b,44));}
void encodeMotion(const Motion&m,uint32_t flags,uint8_t*b){std::memset(b,0,48);std::memcpy(b,"RSM1",4);write32(b+4,48);write64(b+8,m.ms);const float a[]={m.ax,m.ay,m.az,m.gx,m.gy,m.gz};for(unsigned i=0;i<6;++i)putFloat(b+16+4*i,a[i]);write32(b+40,flags);write32(b+44,crc32(b,44));}
bool validRecord(const uint8_t*b,size_t n){return b&&n==48&&(!std::memcmp(b,"RSE1",4)||!std::memcmp(b,"RSM1",4)||!std::memcmp(b,"RSZ1",4))&&read32(b+4)==48&&read32(b+44)==crc32(b,44);}
}
