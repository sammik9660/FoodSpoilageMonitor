#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include "../firmware/esp32s3/gateway/AiFoodModel.h"
#include "../firmware/esp32s3/gateway/AiAnomaly.h"
int main(int argc,char** argv){
  assert(argc==2);std::ifstream f(argv[1]);assert(f);
  ai_anomaly::Model anomaly;int group,last=-1,valid,expectedClass,rows=0,foodCount=0,anomalyCount=0;
  long long at;float t,h,p,gas[10];double expectedMargin,expectedAnomaly;
  while(f>>group>>at>>t>>h>>p){for(float& x:gas)f>>x;
    f>>valid>>expectedClass>>expectedMargin>>expectedAnomaly;
    if(group!=last){anomaly.reset();last=group;}
    auto food=ai_food::predict(gas,t,h,p);
    if(valid){assert(food.measured&&food.candidate==expectedClass);
      assert(std::fabs(food.margin-expectedMargin)<.03);++foodCount;}
    else assert(!food.measured);
    auto a=anomaly.observe(gas,at);
    if(expectedAnomaly<0)assert(!a.valid);
    else {assert(a.valid&&std::fabs(a.index-expectedAnomaly)<.01);++anomalyCount;}
    ++rows;
  }
  assert(rows==3385&&foodCount==3093&&anomalyCount>1000);
  std::cout<<"PASS edge parity rows="<<rows<<" food="<<foodCount<<" anomaly="<<anomalyCount<<"\n";
}
