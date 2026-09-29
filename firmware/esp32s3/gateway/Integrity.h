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
struct Frame { uint32_t boot, seq, ms; float t, h, p, g; };
struct Health { uint32_t boot, ms, failures, recoveries, attempts, txErrors, ok; };
struct Sample {
  char boot[32];  // fixed-width gateway UUID hex, not a C string
  uint32_t rx, nrfBoot, seq, ms;
  uint64_t receivedUptime;
  int64_t capturedAt;
  float t, h, p, g;
  uint32_t timeQuality;  // 0 = unknown, 1 = NTP at capture; never arrival time
  uint32_t checksum;
};
static_assert(sizeof(Sample) == 88, "Persistent schema changed: migrate spool, do not reinterpret");
inline uint32_t crc(const void* ptr, size_t n) {
  const uint8_t* p = (const uint8_t*)ptr;
  uint32_t c = 0xffffffff;
  while (n--) { c ^= *p++; for (int i=0;i<8;++i) c = (c>>1) ^ (0xedb88320u & (0u-(c&1))); }
  return ~c;
}
inline void seal(Sample& s) { s.checksum = crc(&s, offsetof(Sample, checksum)); }
inline bool intact(const Sample& s) { return s.checksum == crc(&s, offsetof(Sample, checksum)); }
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
  if(!literal(p,"STAT,BID=")) return false;
  const char* hex=p;
  return integer(p,h.boot,16) && p-hex==8 && literal(p,",MS=") && integer(p,h.ms) &&
    literal(p,",FAIL=") && integer(p,h.failures) && literal(p,",REC=") && integer(p,h.recoveries) &&
    literal(p,",TRY=") && integer(p,h.attempts) && literal(p,",TXERR=") && integer(p,h.txErrors) &&
    literal(p,",OK=") && integer(p,h.ok) && h.ok<=1 && !*p;
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
} // namespace integrity
