#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstddef>
#include "Integrity.h"
// Trained on first six quality-accepted scans of one explicit STO session.
namespace ai_anomaly {
struct State {uint32_t count=0,scores=0;double sum[10]={},sumSq[10]={},last[3]={};int64_t at[3]={};};
struct Result {bool valid=false,baselineReady=false;double index=0,slopePerHour=0;bool slopeValid=false;};
class Model {
 public:
  State state={};
  void reset(){state=State{};}
  bool restore(const State& s){
    if(s.count>6||s.scores>3)return false;
    for(double x:s.sum)if(!std::isfinite(x))return false;
    for(double x:s.sumSq)if(!std::isfinite(x))return false;
    for(double x:s.last)if(!std::isfinite(x))return false;
    state=s;return true;
  }
  Result observe(const float gas[10],int64_t at){
    double v[10],low=1e100,high=-1e100;unsigned floor=0;
    if(at<=0)return {};
    for(int i=0;i<10;++i){if(!std::isfinite(gas[i])||gas[i]<=0)return {};
      if(std::fabs(double(gas[i])-5.68)<=0.01)++floor;
      v[i]=std::log10(double(gas[i]));if(v[i]<low)low=v[i];if(v[i]>high)high=v[i];}
    if(floor>=8||high-low<.05)return {};
    if(state.count<6){for(int i=0;i<10;++i){state.sum[i]+=v[i];state.sumSq[i]+=v[i]*v[i];}++state.count;
      return {false,state.count==6,0,0,false};}
    double index=0;
    for(int i=0;i<10;++i){const double mean=state.sum[i]/6;
      const double variance=(state.sumSq[i]-state.sum[i]*state.sum[i]/6)/5;
      const double stddev=std::sqrt(variance>0?variance:0);
      index+=std::fabs(v[i]-mean)/(stddev>.05?stddev:.05)/10;}
    if(!std::isfinite(index))return {};
    if(state.scores<3)++state.scores;
    for(int i=0;i<2;++i){state.last[i]=state.last[i+1];state.at[i]=state.at[i+1];}
    state.last[2]=index;state.at[2]=at;
    Result r={true,true,index,0,false};
    if(state.scores==3&&state.at[2]>state.at[0]){r.slopePerHour=(state.last[2]-state.last[0])*3600000.0/(state.at[2]-state.at[0]);r.slopeValid=true;}
    return r;
  }
};
struct Blob {uint32_t magic=0;char storage[48]={},sensor[17]={};int64_t started=0;State state={};uint32_t checksum=0;};
constexpr uint32_t MAGIC=0x414E4F31;
inline void seal(Blob& b){b.magic=MAGIC;b.checksum=integrity::crc(&b,offsetof(Blob,checksum));}
inline bool validFor(const Blob& b,const char* storage,const char* sensor,int64_t started){
  return b.magic==MAGIC&&b.checksum==integrity::crc(&b,offsetof(Blob,checksum))&&
    b.storage[sizeof(b.storage)-1]==0&&b.sensor[sizeof(b.sensor)-1]==0&&
    std::strcmp(b.storage,storage)==0&&std::strcmp(b.sensor,sensor)==0&&b.started==started;}
}
