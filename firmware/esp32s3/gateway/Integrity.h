#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>

namespace integrity {
constexpr size_t MAX_FRAME = 200;
constexpr size_t BATCH_SIZE = 32;
constexpr size_t RAM_SAMPLES = 128;
constexpr uint64_t ENV_HISTORY_INTERVAL_MS = 300000;
inline uint32_t retryWait(uint32_t delay,int http,uint32_t normalMax,uint32_t rateWait) {
  if(http==429||http==503)return rateWait;
  return delay>normalMax?normalMax:delay;
}
inline uint32_t retryNext(uint32_t delay,int http,uint32_t normalMax,uint32_t rateWait) {
  const uint32_t cap=http==429||http==503?rateWait:normalMax;
  return delay>=cap/2?cap:delay*2;
}
struct Frame {
  uint32_t boot=0, seq=0, ms=0, scan=0;
  float t=0, h=0, p=0, g=0;
  uint8_t version=2, step=0, gasIndex=0, measIndex=0, status=0;
};
struct Health { uint32_t boot, ms, failures, recoveries, attempts, txErrors, ok;
  uint32_t batteryMillivolts=0, batteryMinMv=0, batteryMaxMv=0, batteryMeasuredAt=0;
  uint8_t mode=0, envOK=0, gasOK=0, batteryKnown=0, usbPower=0;
  uint8_t batteryPercent=0, batteryState=0, batteryExtended=0, batteryLastPercent=255;
}; // mode 0=legacy, 1=ENV, 2=GAS, 3=COOL, 4=REC
// On-disk v2 layout is immutable. A deployed gateway may still have these
// unacknowledged chunks when the v3 firmware boots.
struct LegacySample {
  char boot[32];  // fixed-width gateway UUID hex, not a C string
  uint32_t rx, nrfBoot, seq, ms;
  uint64_t receivedUptime;
  int64_t capturedAt;
  float t, h, p, g;
  uint32_t timeQuality;  // 0 = unknown, 1 = NTP at capture; never arrival time
  uint32_t checksum;
};
static_assert(sizeof(LegacySample) == 88, "v2 spool layout changed");
struct Sample {
  char boot[32];
  uint32_t rx, nrfBoot, seq, ms;
  uint64_t receivedUptime;
  int64_t capturedAt;
  float t, h, p, g;
  uint32_t timeQuality;
  uint32_t schema, scan, profile;
  uint32_t step, gasIndex, measIndex, sensorStatus;
  uint32_t reserved; // explicit zero makes the full 120-byte record CRC-covered
  uint32_t checksum;
};
static_assert(sizeof(Sample) == 120, "v3 spool layout changed; migrate explicitly");
inline uint32_t crc(const void* ptr, size_t n) {
  const uint8_t* p = (const uint8_t*)ptr;
  uint32_t c = 0xffffffff;
  while (n--) { c ^= *p++; for (int i=0;i<8;++i) c = (c>>1) ^ (0xedb88320u & (0u-(c&1))); }
  return ~c;
}
inline void seal(Sample& s) { s.checksum = crc(&s, offsetof(Sample, checksum)); }
inline bool intact(const Sample& s) { return s.checksum == crc(&s, offsetof(Sample, checksum)); }
inline bool intact(const LegacySample& s) { return s.checksum == crc(&s, offsetof(LegacySample, checksum)); }
inline Sample promote(const LegacySample& old) {
  Sample s={};
  memcpy(s.boot,old.boot,32);
  s.rx=old.rx; s.nrfBoot=old.nrfBoot; s.seq=old.seq; s.ms=old.ms;
  s.receivedUptime=old.receivedUptime; s.capturedAt=old.capturedAt;
  s.t=old.t; s.h=old.h; s.p=old.p; s.g=old.g;
  s.timeQuality=old.timeQuality; s.schema=2; seal(s);
  return s;
}
struct PendingBatch {
  Sample samples[BATCH_SIZE] = {};
  uint32_t count=0;
  bool release(bool applicationAck) {
    if(!applicationAck) return false;
    count=0; return true;
  }
};
inline bool literal(const char*& p, const char* key) {
  size_t n = strlen(key); if (strncmp(p,key,n)) return false; p += n; return true;
}
inline bool integer(const char*& p, uint32_t& out, unsigned base=10) {
  const char* start=p;
  uint64_t v=0;
  while (*p) {
    unsigned d = *p>='0'&&*p<='9' ? *p-'0' : *p>='A'&&*p<='F' ? *p-'A'+10 : 99;
    if (d>=base) break;
    v=v*base+d; if(v>0xffffffffULL) return false; ++p;
  }
  if(p==start) return false; out=(uint32_t)v; return true;
}
inline bool decimal(const char*& p, float& out) {
  const char* start=p;
  if (*p=='-') ++p;
  const char* digits=p; while(*p>='0'&&*p<='9') ++p;
  if(p==digits) return false;
  if(*p=='.') { ++p; digits=p; while(*p>='0'&&*p<='9') ++p; if(p==digits) return false; }
  errno=0; char* end=nullptr; out=strtof(start,&end);
  return end==p && errno!=ERANGE && isfinite(out);
}
inline bool parseFrame(const char* p, Frame& f) {
  f=Frame{};
  if(literal(p,"ENV,BID=")) {
    f.version=4;const char* hex=p;
    if(!integer(p,f.boot,16)||p-hex!=8||
       !literal(p,",SEQ=")||!integer(p,f.seq)||!f.seq||
       !literal(p,",MS=")||!integer(p,f.ms)||
       !literal(p,",T=")||!decimal(p,f.t)||
       !literal(p,",H=")||!decimal(p,f.h)||
       !literal(p,",P=")||!decimal(p,f.p)||*p)return false;
    return f.t>=-40&&f.t<=85&&f.h>=0&&f.h<=100&&f.p>=300&&f.p<=1100;
  }
  if(literal(p,"V=3,")) {
    f.version=3;
    const char* hex;
    uint32_t step=0, gasIndex=0, measIndex=0, status=0;
    if(!literal(p,"BID=")) return false;
    hex=p; if(!integer(p,f.boot,16)||p-hex!=8) return false;
    if(!literal(p,",SEQ=")||!integer(p,f.seq)||!f.seq||
       !literal(p,",SCAN=")||!integer(p,f.scan)||!f.scan||
       !literal(p,",STEP=")||!integer(p,step)||step>9||
       !literal(p,",GI=")||!integer(p,gasIndex)||gasIndex!=step||
       !literal(p,",MI=")||!integer(p,measIndex)||measIndex>255||
       !literal(p,",MS=")||!integer(p,f.ms)||
       !literal(p,",ST=")||!integer(p,status)||status>255||
       (status&0xb0)!=0xb0||
       !literal(p,",PID=354,T=")||!decimal(p,f.t)||
       !literal(p,",H=")||!decimal(p,f.h)||
       !literal(p,",P=")||!decimal(p,f.p)||
       !literal(p,",G=")||!decimal(p,f.g)||*p) return false;
    f.step=(uint8_t)step; f.gasIndex=(uint8_t)gasIndex;
    f.measIndex=(uint8_t)measIndex; f.status=(uint8_t)status;
    return f.t>=-40&&f.t<=85&&f.h>=0&&f.h<=100&&
           f.p>=300&&f.p<=1100&&f.g>0;
  }
  const char* hex;
  if(!literal(p,"BID=")) return false;
  hex=p; if(!integer(p,f.boot,16)||p-hex!=8) return false;
  return literal(p,",SEQ=") && integer(p,f.seq) && f.seq>0 &&
    literal(p,",MS=") && integer(p,f.ms) &&
    literal(p,",T=") && decimal(p,f.t) && literal(p,",H=") && decimal(p,f.h) &&
    literal(p,",P=") && decimal(p,f.p) && literal(p,",G=") && decimal(p,f.g) &&
    *p==0 && f.t>=-40 && f.t<=85 && f.h>=0 && f.h<=100 && f.p>=300 && f.p<=1100 && f.g>=0;
}
inline bool parseHealth(const char* p, Health& h) {
  h=Health{};
  if(!literal(p,"STAT,BID=")) return false;
  const char* hex=p;
  if(!(integer(p,h.boot,16) && p-hex==8 && literal(p,",MS=") && integer(p,h.ms) &&
    literal(p,",FAIL=") && integer(p,h.failures) && literal(p,",REC=") && integer(p,h.recoveries) &&
    literal(p,",TRY=") && integer(p,h.attempts) && literal(p,",TXERR=") && integer(p,h.txErrors) &&
    literal(p,",OK=") && integer(p,h.ok) && h.ok<=1))return false;
  if(!*p)return true; // v2/v3 health remains valid without mode fields
  if(!literal(p,",MODE="))return false;
  if(literal(p,"ENV"))h.mode=1;
  else if(literal(p,"GAS"))h.mode=2;
  else if(literal(p,"COOL"))h.mode=3;
  else if(literal(p,"REC"))h.mode=4;
  else return false;
  uint32_t env=0,gas=0;
  if(!literal(p,",EOK=")||!integer(p,env)||env>1||
     !literal(p,",GOK=")||!integer(p,gas)||gas>1)return false;
  h.envOK=(uint8_t)env;h.gasOK=(uint8_t)gas;
  if(!*p)return true; // Existing sensor firmware has no battery fields.
  uint32_t known=0,usb=0;
  if(!literal(p,",BV=")||!integer(p,h.batteryMillivolts)||h.batteryMillivolts>5500||
     !literal(p,",BOK=")||!integer(p,known)||known>1||
     !literal(p,",USB=")||!integer(p,usb)||usb>1)return false;
  if((known&&(!h.batteryMillivolts||usb))||(!known&&h.batteryMillivolts))return false;
  h.batteryKnown=(uint8_t)known;h.usbPower=(uint8_t)usb;
  if(!*p)return true; // BV/BOK/USB-only sensor firmware remains compatible.
  uint32_t pct=0,state=0,minMv=0,maxMv=0;
  if(!literal(p,",BP=")||!integer(p,pct)||pct>100||
     !literal(p,",BS=")||!integer(p,state)||state>4||
     !literal(p,",MN=")||!integer(p,minMv)||minMv>5500||
     !literal(p,",MX=")||!integer(p,maxMv)||maxMv>5500)return false;
  if((minMv&&minMv<2500)||(maxMv&&maxMv<minMv)||(known&&(!minMv||!maxMv))||(!known&&pct))return false;
  h.batteryPercent=(uint8_t)pct;h.batteryState=(uint8_t)state;
  h.batteryMinMv=minMv;h.batteryMaxMv=maxMv;h.batteryExtended=1;
  if(!*p)return true; // Extended firmware before battery measurement uptime.
  if(!literal(p,",BM=")||!integer(p,h.batteryMeasuredAt))return false;
  if(!*p)return true;
  uint32_t lastPercent=0;
  if(!literal(p,",BLP=")||!integer(p,lastPercent)||lastPercent>255||*p)return false;
  h.batteryLastPercent=(uint8_t)lastPercent;
  return lastPercent<=100 || lastPercent==255;
}
struct Framer {
  char line[MAX_FRAME+1] = {};
  size_t length=0;
  bool discard=false, cr=false;
  uint32_t overflows=0;
  void reset() { length=0; discard=false; cr=false; }
  // 1 complete line, -1 invalid/overflow, 0 incomplete/empty.
  int feed(char c) {
    if(c=='\n') {
      if(discard) { reset(); return 0; }
      line[length]=0; bool nonempty=length>0; length=0; cr=false; return nonempty?1:0;
    }
    if(discard) return 0;
    if(c=='\r' && !cr) { cr=true; return 0; }
    if(cr || c<32 || c>126 || length==MAX_FRAME) {
      discard=true; length=0; ++overflows; return -1;
    }
    line[length++]=c; return 0;
  }
};
struct Continuity {
  bool known=false; uint32_t boot=0, seq=0, gaps=0, restarts=0, duplicates=0;
  bool accept(const Frame& f) {
    if(known && f.boot==boot) {
      if(f.seq<=seq) { ++duplicates; return false; }
      gaps += f.seq-seq-1;
    } else if(known) ++restarts;
    known=true; boot=f.boot; seq=f.seq; return true;
  }
};
struct EnvironmentState {
  Continuity continuity;
  Frame latest={};
  uint64_t receivedAt=0, nextHistoryAt=0;
  bool known=false;
  bool accept(const Frame& f,uint64_t at,bool& historyDue) {
    historyDue=false;
    if(f.version!=4||!continuity.accept(f))return false;
    latest=f;receivedAt=at;known=true;
    historyDue=nextHistoryAt==0||at>=nextHistoryAt;
    if(historyDue)nextHistoryAt=at+ENV_HISTORY_INTERVAL_MS;
    return true;
  }
  void historyRejected(){nextHistoryAt=0;}
};
} // namespace integrity
