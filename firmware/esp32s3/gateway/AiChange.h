#pragma once
#include <cmath>
#include <cstdint>

// Shadow-only, session-relative gas change. This is not a spoilage probability.
namespace ai_change {
struct Result { bool valid=false,baseline_ready=false; double change=0,shape=0,level=0; };
struct State { unsigned count=0;double baseline[10]={},mean=0; };
class Model {
 public:
  void reset() { count_=0;for(double& x:baseline_)x=0;baselineMean_=0; }
  unsigned baselineCount() const { return count_; }
  State snapshot() const { State s;s.count=count_;for(int i=0;i<10;++i)s.baseline[i]=baseline_[i];s.mean=baselineMean_;return s; }
  bool restore(const State& s) {
    if(s.count>3||!std::isfinite(s.mean))return false;
    for(double x:s.baseline)if(!std::isfinite(x))return false;
    count_=s.count;for(int i=0;i<10;++i)baseline_[i]=s.baseline[i];baselineMean_=s.mean;return true;
  }
  Result observe(const float gas[10]) {
    double logGas[10],mean=0,lo=1e100,hi=-1e100;unsigned floor=0;
    for(int i=0;i<10;++i){
      if(!std::isfinite(gas[i])||gas[i]<=0)return {};
      if(std::fabs(double(gas[i])-5.68)<=0.01)++floor;
      logGas[i]=std::log10(double(gas[i]));mean+=logGas[i]/10;
      if(logGas[i]<lo)lo=logGas[i];if(logGas[i]>hi)hi=logGas[i];
    }
    if(floor>=8||hi-lo<0.05)return {};
    if(count_<3){
      for(int i=0;i<10;++i)baseline_[i]+=(logGas[i]-mean)/3;
      baselineMean_+=mean/3;++count_;return {false,count_==3,0,0,0};
    }
    double shape=0;
    for(int i=0;i<10;++i)shape+=std::fabs((logGas[i]-mean)-baseline_[i])/10;
    const double level=std::fabs(mean-baselineMean_);
    return {true,true,0.7*shape+0.3*level,shape,level};
  }
 private:
  unsigned count_=0;double baseline_[10]={},baselineMean_=0;
};
} // namespace ai_change
