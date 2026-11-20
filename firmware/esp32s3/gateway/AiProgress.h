#pragma once
#include <cmath>
#include <cstring>
// Generated food/time fallback from the same weak-label model as FoodProgressModel.gs.
// Seven scalar features need no tensor arena or SIMD kernel.
namespace ai_progress {
constexpr const char* MODEL="assumed-progress-v1-time";
constexpr const char* FOODS[6]={"banana","chicken","egg","rice","sprouts","tofu"};
constexpr double MEAN[7]={0.1237928007023705,0.24143985952589991,0.17910447761194029,0.31167690956979804,0.088674275680421424,0.055311676909569799,3.2093314832948017};
constexpr double SCALE[7]={0.32934502151487527,0.4279563689887248,0.38343977846761584,0.4631785979628355,0.28427301756053819,0.22858760969707251,1.2085433454732544};
constexpr double WEIGHTS[7]={0.53468294566946784,0.074605928301437446,-0.44060117248682901,-0.54824114691598924,-0.41275786901957201,1.4532295022779942,6.4309086170792344};
constexpr double INTERCEPT=-0.7172924222237228;
struct Result {bool valid=false;int index=0;const char* stage="UNAVAILABLE";};
inline int foodIndex(const char* food){if(!food)return -1;for(int i=0;i<6;++i)
  if(!std::strcmp(food,FOODS[i]))return i;return -1;}
inline Result predict(int food,double elapsedHours){
  if(food<0||food>=6||!std::isfinite(elapsedHours)||elapsedHours<0)return {};
  double score=INTERCEPT;
  for(int i=0;i<6;++i){const double v=i==food?1.0:0.0;
    score+=WEIGHTS[i]*(v-MEAN[i])/SCALE[i];}
  score+=WEIGHTS[6]*(std::log1p(elapsedHours)-MEAN[6])/SCALE[6];
  const double bounded=score<-30?-30:score>30?30:score;
  const int index=int(std::round(100.0/(1.0+std::exp(-bounded))));
  return {true,index,index<33?"EARLY":index<67?"MIDDLE":"LATE"};
}
} // namespace ai_progress
