#pragma once
#include <cstdint>
#include <cmath>

// Passive observer of already accepted BLE v3 frames. Never repairs a missing step.
namespace ai_scan {
struct Step { uint32_t boot,scan,seq,ms;uint8_t heater,gas,mi,status;float resistance,t,h,p; };
struct Complete { uint32_t boot=0,scan=0,firstMs=0,lastMs=0;float gas[10]={};float t=0,h=0,p=0; };
class Assembler {
 public:
  void reset(){next_=0;active_=false;sumT_=sumH_=sumP_=0;}
  bool observe(const Step& s,Complete& out){
    if(s.heater==0){if(active_&&next_>0){++partialScans;lastPartialMask=(uint16_t)((1u<<next_)-1u);}
      reset();active_=true;boot_=s.boot;scan_=s.scan;firstMs_=s.ms;}
    if(!active_||s.heater!=next_||s.gas!=s.heater||s.boot!=boot_||s.scan!=scan_||
       (s.status&0xb0)!=0xb0||!std::isfinite(s.resistance)||s.resistance<=0||
       !std::isfinite(s.t)||!std::isfinite(s.h)||!std::isfinite(s.p)||
       (next_&&((uint8_t)(s.mi-lastMi_)==0||(uint8_t)(s.mi-lastMi_)>=128))||
       (next_&&s.ms<lastMs_)){++rejectedFields;reset();return false;}
    values_[next_]=s.resistance;lastMi_=s.mi;lastMs_=s.ms;
    sumT_+=s.t;sumH_+=s.h;sumP_+=s.p;
    if(++next_!=10)return false;
    out.boot=boot_;out.scan=scan_;out.firstMs=firstMs_;out.lastMs=lastMs_;
    out.t=sumT_/10;out.h=sumH_/10;out.p=sumP_/10;
    for(int i=0;i<10;++i)out.gas[i]=values_[i];reset();return true;
  }
  uint32_t partialScans=0,rejectedFields=0;uint16_t lastPartialMask=0;
 private:
  float values_[10]={},sumT_=0,sumH_=0,sumP_=0;uint32_t boot_=0,scan_=0,firstMs_=0,lastMs_=0;
  uint8_t next_=0,lastMi_=0;bool active_=false;
};
} // namespace ai_scan
