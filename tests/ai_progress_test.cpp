#include <cassert>
#include <cstdio>
#include <cstring>
#include "../firmware/esp32s3/gateway/AiProgress.h"

int main(){
  using ai_progress::predict;
  assert(ai_progress::foodIndex("sprouts")==4);
  assert(ai_progress::foodIndex("unknown")==-1);
  assert(!predict(-1,10).valid);
  assert(!predict(4,-1).valid);
  struct Vector {const char* food;double hours;int index;};
  const Vector vectors[]={{"banana",1,0},{"banana",24,74},{"rice",24,15},
    {"rice",72,98},{"sprouts",24,12},{"sprouts",72,98},{"sprouts",120,100}};
  for(const auto& v:vectors){const auto result=predict(ai_progress::foodIndex(v.food),v.hours);
    assert(result.valid&&result.index==v.index);
    assert(!std::strcmp(result.stage,v.index<33?"EARLY":v.index<67?"MIDDLE":"LATE"));}
  std::puts("PASS ESP time progression: generated Python coefficients and stage thresholds");
}
