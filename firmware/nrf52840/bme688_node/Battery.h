
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace battery {

constexpr uint16_t FULL_MV = 4200;
constexpr uint16_t EMPTY_MV = 3500;
constexpr uint16_t VALID_MIN_MV = 2500;
constexpr uint16_t VALID_MAX_MV = 5500;
constexpr uint8_t HISTORY_BANDS = 7;
constexpr uint32_t PERSIST_MAGIC = 0x42545459;
constexpr uint16_t PERSIST_VERSION = 1;
constexpr uint32_t PERSIST_INTERVAL_MS = 6UL * 60UL * 60UL * 1000UL;

enum State : uint8_t { DEFAULT=0, COLLECTING=1, CALIBRATED=2, INSUFFICIENT_DATA=3, INVALID=4 };
enum LoadMode : uint8_t { LOAD_ENV=0, LOAD_GAS=1, LOAD_COOL=2, LOAD_REC=3 };
struct Point { uint16_t mv; uint8_t percent; };

// Conservative estimate for a common 4.2 V Li-Po. It is not a capacity meter.
constexpr Point CURVE[] = {
  {3500,0}, {3600,8}, {3700,20}, {3800,40},
  {3900,60}, {4000,75}, {4100,90}, {4200,100}
};

inline bool validVoltage(uint32_t mv) { return mv>=VALID_MIN_MV && mv<=VALID_MAX_MV; }
inline uint8_t percent(uint32_t mv) {
  if(mv<=EMPTY_MV)return 0;if(mv>=FULL_MV)return 100;
  for(unsigned i=1;i<sizeof(CURVE)/sizeof(CURVE[0]);++i)if(mv<=CURVE[i].mv){
    const Point& a=CURVE[i-1];const Point& b=CURVE[i];
    return (uint8_t)(a.percent+(mv-a.mv)*(b.percent-a.percent)/(b.mv-a.mv));
  }
  return 100;
}

inline uint32_t crc32(const void* data,size_t length){
  const uint8_t* p=(const uint8_t*)data;uint32_t crc=0xffffffffu;
  while(length--){crc^=*p++;for(uint8_t bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1u)));}
  return ~crc;
}

struct PersistedState {
  uint32_t magic=PERSIST_MAGIC;uint16_t version=PERSIST_VERSION;uint16_t size=sizeof(PersistedState);
  uint32_t generation=0,minMv=UINT32_MAX,maxMv=0,validSamples=0,stableSeconds=0;
  uint32_t bandSeconds[HISTORY_BANDS]={};uint32_t fallingSteps=0,risingSteps=0;
  uint8_t state=DEFAULT,reserved[3]={};uint32_t crc=0;
};
inline void seal(PersistedState& s){s.magic=PERSIST_MAGIC;s.version=PERSIST_VERSION;s.size=sizeof(s);s.crc=crc32(&s,offsetof(PersistedState,crc));}
inline bool intact(const PersistedState& s){
  if(s.magic!=PERSIST_MAGIC||s.version!=PERSIST_VERSION||s.size!=sizeof(s)||s.state>INVALID||
     s.minMv<VALID_MIN_MV||s.minMv>VALID_MAX_MV||s.maxMv<s.minMv||s.maxMv>VALID_MAX_MV)return false;
  return s.crc==crc32(&s,offsetof(PersistedState,crc));
}
inline const char* stateName(State state){
  switch(state){case DEFAULT:return "DEFAULT";case COLLECTING:return "COLLECTING";
    case CALIBRATED:return "CALIBRATED";case INSUFFICIENT_DATA:return "INSUFFICIENT_DATA";default:return "INVALID";}
}

struct Tracker {
  uint32_t minMv=UINT32_MAX,maxMv=0,filteredMv=0,validSamples=0,stableSeconds=0;
  uint32_t bandSeconds[HISTORY_BANDS]={};uint32_t fallingSteps=0,risingSteps=0,generation=0;
  State state=DEFAULT;uint32_t recent[5]={};uint8_t recentCount=0,recentNext=0;
  uint32_t lastAt=0,lastStableMv=0,lastPersistAt=0,persistedMinMv=UINT32_MAX,persistedMaxMv=0;
  uint32_t persistedStableSeconds=0;
  State persistedState=DEFAULT;bool restored=false;

  static uint32_t median(uint32_t* values,uint8_t count){
    uint32_t sorted[5]={};for(uint8_t i=0;i<count;++i)sorted[i]=values[i];
    for(uint8_t i=1;i<count;++i){uint32_t v=sorted[i];uint8_t j=i;while(j&&sorted[j-1]>v){sorted[j]=sorted[j-1];--j;}sorted[j]=v;}
    return sorted[count/2];
  }
  uint8_t occupiedBands() const {uint8_t n=0;for(uint8_t i=0;i<HISTORY_BANDS;++i)if(bandSeconds[i]>=600)++n;return n;}
  bool enoughForLimitedCalibration() const {
    return stableSeconds>=8UL*60UL*60UL&&maxMv>=4100&&minMv<=3650&&maxMv-minMv>=450&&
           occupiedBands()>=5&&fallingSteps>=20&&fallingSteps>=risingSteps*4;
  }
  void updateState(){
    if(enoughForLimitedCalibration())state=CALIBRATED;
    else if(validSamples>=24UL*60UL)state=INSUFFICIENT_DATA;
    else if(validSamples>=6)state=COLLECTING;
    else if(state!=INVALID)state=DEFAULT;
  }
  bool observe(uint32_t mv,uint32_t now,LoadMode mode){
    if(!validVoltage(mv))return false;
    recent[recentNext]=mv;recentNext=(uint8_t)((recentNext+1)%5);if(recentCount<5)++recentCount;
    filteredMv=median(recent,recentCount);if(mv<minMv)minMv=mv;if(mv>maxMv)maxMv=mv;++validSamples;
    // Only heater-off ENV time trains the model. GAS/COOL sag cannot become SOC.
    if(mode==LOAD_ENV&&lastAt){const uint32_t elapsed=now-lastAt;
      if(elapsed>=30000&&elapsed<=120000&&lastStableMv){const uint32_t seconds=elapsed/1000;
        const uint32_t bounded=lastStableMv<EMPTY_MV?EMPTY_MV:lastStableMv>=FULL_MV?FULL_MV-1:lastStableMv;
        bandSeconds[(bounded-EMPTY_MV)/100]+=seconds;stableSeconds+=seconds;
        if(filteredMv<lastStableMv)++fallingSteps;else if(filteredMv>lastStableMv+5)++risingSteps;}
      lastStableMv=filteredMv;
    }else if(mode==LOAD_ENV)lastStableMv=filteredMv;
    lastAt=mode==LOAD_ENV?now:0;updateState();return true;
  }
  uint8_t estimatedPercent() const {
    const uint32_t mv=filteredMv?filteredMv:(maxMv?maxMv:0);const uint8_t baseline=percent(mv);
    if(state!=CALIBRATED||!mv)return baseline;
    uint64_t total=0,below=0;for(uint8_t i=0;i<HISTORY_BANDS;++i)total+=bandSeconds[i];if(!total)return baseline;
    const uint32_t bounded=mv<=EMPTY_MV?EMPTY_MV:mv>=FULL_MV?FULL_MV:mv;
    const uint8_t band=bounded==FULL_MV?HISTORY_BANDS:(uint8_t)((bounded-EMPTY_MV)/100);
    for(uint8_t i=0;i<band&&i<HISTORY_BANDS;++i)below+=bandSeconds[i];
    if(band<HISTORY_BANDS)below+=(uint64_t)bandSeconds[band]*((bounded-EMPTY_MV)%100)/100;
    int learned=(int)((below*100+total/2)/total),blended=((int)baseline+learned)/2;
    if(blended>(int)baseline+10)blended=baseline+10;if(blended<(int)baseline-10)blended=baseline-10;
    if(blended<0)blended=0;if(blended>100)blended=100;return (uint8_t)blended;
  }
  PersistedState snapshot() const {PersistedState s;s.generation=generation+1;s.minMv=minMv;s.maxMv=maxMv;
    s.validSamples=validSamples;s.stableSeconds=stableSeconds;memcpy(s.bandSeconds,bandSeconds,sizeof(bandSeconds));
    s.fallingSteps=fallingSteps;s.risingSteps=risingSteps;s.state=state;seal(s);return s;}
  bool restore(const PersistedState& s){if(!intact(s)){state=INVALID;return false;}generation=s.generation;minMv=s.minMv;maxMv=s.maxMv;
    validSamples=s.validSamples;stableSeconds=s.stableSeconds;memcpy(bandSeconds,s.bandSeconds,sizeof(bandSeconds));
    fallingSteps=s.fallingSteps;risingSteps=s.risingSteps;state=(State)s.state;persistedState=state;
    persistedMinMv=minMv;persistedMaxMv=maxMv;persistedStableSeconds=stableSeconds;restored=true;return true;}
  bool shouldPersist(uint32_t now) const {if(validSamples<6||minMv==UINT32_MAX)return false;
    const bool changed=state!=persistedState||persistedMinMv==UINT32_MAX||minMv+50<=persistedMinMv||maxMv>=persistedMaxMv+50||
      stableSeconds>=persistedStableSeconds+6UL*60UL*60UL;
    return changed&&(!lastPersistAt||(uint32_t)(now-lastPersistAt)>=PERSIST_INTERVAL_MS);}
  void persisted(uint32_t now,uint32_t newGeneration){generation=newGeneration;lastPersistAt=now;
    persistedMinMv=minMv;persistedMaxMv=maxMv;persistedStableSeconds=stableSeconds;persistedState=state;}
};

} // namespace battery
